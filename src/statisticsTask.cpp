/**
 * @file statisticsTask.cpp
 * @brief Statistics Task implementation
 * 
 * Monitors, aggregates, and tracks system performance metrics and operational
 * statistics for RTK/NTRIP client operation, GPS fix quality, and overall system health.
 */

#include "statisticsTask.h"
#include "gnssReceiverTask.h"
#include "ntripClientTask.h"
#include "dataOutputTask.h"
#include "ledIndicatorTask.h"
#include "wifiManager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <string.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "StatsTask";

// Task configuration
#define STATS_TASK_STACK_SIZE   4096
#define STATS_TASK_PRIORITY     1
#define STATS_UPDATE_RATE_MS    1000

// Task handle
static TaskHandle_t stats_task_handle = NULL;

// Per-task loop-time accumulators are owned by each task module and queried
// via the task_get_avg_loop_us_and_reset() functions below. The index in the
// cpu_usage_percent[] / avg_task_loop_time_ms[] arrays corresponds to:
//   0 = NTRIP task
//   1 = GNSS task
//   2 = Data Output task
//   3 = LED Indicator task
//   4 = Statistics task itself
#define TASK_IDX_NTRIP     0
#define TASK_IDX_GNSS      1
#define TASK_IDX_DATAOUT   2
#define TASK_IDX_LED       3
#define TASK_IDX_STATS     4
#define TRACKED_TASK_COUNT 5

// Forward declarations of task-module getters. Each is implemented in its
// own .cpp file. Returns 0 if the task hasn't started yet (NULL handle).
extern "C" {
    TaskHandle_t ntrip_task_get_handle(void);
    TaskHandle_t gnss_task_get_handle(void);
    TaskHandle_t data_output_task_get_handle(void);
    TaskHandle_t led_task_get_handle(void);

    uint32_t ntrip_task_get_avg_loop_us_and_reset(void);
    uint32_t gnss_task_get_avg_loop_us_and_reset(void);
    uint32_t data_output_task_get_avg_loop_us_and_reset(void);
    uint32_t led_task_get_avg_loop_us_and_reset(void);

    uint32_t data_output_get_tx_count_and_reset(void);
}

// Statistics task's own loop-time accumulator (we measure ourselves too).
static uint64_t stats_loop_time_sum_us = 0;
static uint32_t stats_loop_count = 0;

// Statistics data (protected by mutex)
static system_statistics_t stats;
static SemaphoreHandle_t stats_mutex = NULL;

// Configuration
static statistics_config_t config = {
    .interval_sec = 60,
    .enabled = true,
    .web_api_enable = true,
    .mqtt_publish = false
};

// Internal state tracking
static uint8_t last_fix_quality = 0;
static time_t last_fix_quality_change = 0;
static uint32_t hdop_sample_count = 0;
static float hdop_sum = 0.0f;
static uint32_t sat_sample_count = 0;
static uint32_t sat_sum = 0;
static int32_t rssi_sample_count = 0;
static int32_t rssi_sum = 0;

// RTCM latency tracking
static uint64_t rtcm_latency_sum_ms = 0;
static uint32_t rtcm_latency_count = 0;

// Queue depth tracking (sampled every tick by the stats task)
static uint64_t rtcm_queue_depth_sum = 0;
static uint64_t gga_queue_depth_sum = 0;
static uint32_t queue_depth_sample_count = 0;
// Edge-triggered RTCM watermark warning — one log per period when depth ≥ 75%.
static bool rtcm_watermark_warned_this_period = false;

// NTRIP reconnect timing (between RECONNECT_BEGIN and CONNECTED)
static int64_t ntrip_reconnect_begin_us = 0;
static uint64_t ntrip_reconnect_time_sum_ms = 0;
static uint32_t ntrip_reconnect_time_count = 0;

// GGA actual interval tracking (delta between successful sends)
static time_t prev_gga_sent_time = 0;
static uint64_t gga_interval_sum_sec = 0;
static uint32_t gga_interval_count = 0;

// CPU usage tracking — previous runtime counter snapshot per tracked task
typedef struct {
    TaskHandle_t handle;             // resolved each period via the *_get_handle() getters
    uint32_t prev_runtime_counter;   // last seen ulRunTimeCounter from uxTaskGetSystemState
} task_cpu_state_t;
static task_cpu_state_t cpu_state[TRACKED_TASK_COUNT];
static uint32_t prev_total_runtime = 0;

// Resolve task handles once; safe to call repeatedly (returns the cached value
// after the task has been created).
static void refresh_task_handles(void) {
    cpu_state[TASK_IDX_NTRIP].handle   = ntrip_task_get_handle();
    cpu_state[TASK_IDX_GNSS].handle    = gnss_task_get_handle();
    cpu_state[TASK_IDX_DATAOUT].handle = data_output_task_get_handle();
    cpu_state[TASK_IDX_LED].handle     = led_task_get_handle();
    cpu_state[TASK_IDX_STATS].handle   = stats_task_handle;
}

/**
 * @brief Initialize statistics structures to zero
 */
static void init_statistics(void) {
    memset(&stats, 0, sizeof(system_statistics_t));

    struct timeval tv;
    gettimeofday(&tv, NULL);
    stats.period_start_time = tv.tv_sec;
    stats.period_start_uptime_sec = 0;  // boot

    // Initialize min values to max possible
    stats.runtime.hdop_min_boot = 99.9f;
    stats.runtime.satellites_min_boot = 255;
    stats.runtime.wifi_rssi_min_boot = INT8_MAX;
    stats.runtime.heap_min_free_bytes = 0xFFFFFFFF;

    stats.period.hdop_min = 99.9f;
    stats.period.satellites_min = 255;
    stats.period.wifi_rssi_min = INT8_MAX;

    memset(cpu_state, 0, sizeof(cpu_state));
    prev_total_runtime = 0;
}

/**
 * @brief Reset period statistics
 */
static void reset_period_stats(void) {
    if (xSemaphoreTake(stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Capture the just-finished period's duration. Use uptime-deltas
        // (monotonic) rather than wall-clock subtraction — wall clock can
        // jump (NTP, RTC sync) and was a source of nonsense in the old code.
        uint32_t uptime_now = (uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ);
        stats.period_duration_sec = uptime_now - stats.period_start_uptime_sec;

        struct timeval tv;
        gettimeofday(&tv, NULL);
        stats.period_start_time = tv.tv_sec;
        stats.period_start_uptime_sec = uptime_now;

        // Reset period structure
        memset(&stats.period, 0, sizeof(period_statistics_t));

        // Reinitialize min sentinels (must come AFTER memset, BEFORE next sample)
        stats.period.hdop_min = 99.9f;
        stats.period.satellites_min = 255;
        stats.period.wifi_rssi_min = INT8_MAX;  // was buggy: was 0, allowed any negative RSSI to look "min"

        // Reset accumulator variables
        hdop_sample_count = 0;
        hdop_sum = 0.0f;
        sat_sample_count = 0;
        sat_sum = 0;
        rssi_sample_count = 0;
        rssi_sum = 0;

        // Reset latency tracking
        rtcm_latency_sum_ms = 0;
        rtcm_latency_count = 0;

        // Allow the RTCM watermark warning to fire again next period.
        rtcm_watermark_warned_this_period = false;

        // Reset queue-depth, reconnect-timing and gga-interval accumulators
        rtcm_queue_depth_sum = 0;
        gga_queue_depth_sum = 0;
        queue_depth_sample_count = 0;
        ntrip_reconnect_time_sum_ms = 0;
        ntrip_reconnect_time_count = 0;
        gga_interval_sum_sec = 0;
        gga_interval_count = 0;

        xSemaphoreGive(stats_mutex);
    }
}

/**
 * @brief Update system uptime and NTRIP uptime
 */
static void update_uptime(void) {
    TickType_t ticks = xTaskGetTickCount();
    uint32_t uptime_sec = ticks / configTICK_RATE_HZ;
    stats.runtime.system_uptime_sec = uptime_sec;
    
    // Update NTRIP uptime from client
    stats.runtime.ntrip_uptime_sec = ntrip_get_uptime_sec();
}

/**
 * @brief Collect heap memory statistics
 */
static void collect_heap_stats(void) {
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_free_heap = esp_get_minimum_free_heap_size();
    uint32_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    
    // Update runtime
    if (min_free_heap < stats.runtime.heap_min_free_bytes) {
        stats.runtime.heap_min_free_bytes = min_free_heap;
    }
    
    // Update period
    stats.period.heap_free_bytes = free_heap;
    stats.period.heap_largest_block = largest_block;
}

/**
 * @brief Collect stack high water marks for all five tracked tasks.
 *
 * The "high water mark" returned by FreeRTOS is the *minimum* free stack the
 * task has ever had (in words). We track the minimum-of-the-mins per task
 * (i.e., the tightest stack pressure observed since boot).
 */
static void collect_stack_hwm(void) {
    struct entry { TaskHandle_t h; uint32_t* dst; };
    refresh_task_handles();
    entry tasks[TRACKED_TASK_COUNT] = {
        { cpu_state[TASK_IDX_NTRIP].handle,   &stats.runtime.stack_hwm_ntrip   },
        { cpu_state[TASK_IDX_GNSS].handle,    &stats.runtime.stack_hwm_gnss    },
        { cpu_state[TASK_IDX_DATAOUT].handle, &stats.runtime.stack_hwm_dataout },
        { cpu_state[TASK_IDX_LED].handle,     &stats.runtime.stack_hwm_led     },
        { cpu_state[TASK_IDX_STATS].handle,   &stats.runtime.stack_hwm_stats   },
    };

    for (int i = 0; i < TRACKED_TASK_COUNT; i++) {
        if (tasks[i].h == NULL) continue;
        UBaseType_t hwm = uxTaskGetStackHighWaterMark(tasks[i].h);
        // Track the *minimum* free stack observed (tightest pressure).
        if (*tasks[i].dst == 0 || hwm < *tasks[i].dst) {
            *tasks[i].dst = (uint32_t)hwm;
        }
    }
}

/**
 * @brief Sample queue depths and update running peak + sum-for-average.
 *
 * Called on every stats-task tick (~1Hz). Peak is runtime-wide. Average is
 * computed from the running sum at log time and reset per period.
 */
static void collect_queue_stats(void) {
    if (rtcm_queue != NULL) {
        UBaseType_t d = uxQueueMessagesWaiting(rtcm_queue);
        UBaseType_t total = d + uxQueueSpacesAvailable(rtcm_queue);
        rtcm_queue_depth_sum += d;
        if ((uint32_t)d > stats.runtime.rtcm_queue_peak_count) {
            stats.runtime.rtcm_queue_peak_count = (uint32_t)d;
        }
        // Edge-triggered: one warning per period if depth crosses 75%. Cleared
        // by reset_period_stats() so we can re-fire next period.
        if (!rtcm_watermark_warned_this_period && total > 0 && d * 4 >= total * 3) {
            ESP_LOGW(TAG, "RTCM queue at %u/%u (>=75%%) — uplink may be lagging",
                     (unsigned)d, (unsigned)total);
            rtcm_watermark_warned_this_period = true;
        }
    }
    if (gga_queue != NULL) {
        UBaseType_t d = uxQueueMessagesWaiting(gga_queue);
        gga_queue_depth_sum += d;
        if ((uint32_t)d > stats.runtime.gga_queue_peak_count) {
            stats.runtime.gga_queue_peak_count = (uint32_t)d;
        }
    }
    queue_depth_sample_count++;
}

/**
 * @brief Snapshot per-task CPU runtime counters and compute period percentages.
 *
 * Requires CONFIG_FREERTOS_USE_TRACE_FACILITY=y and CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y
 * in sdkconfig. uxTaskGetSystemState() returns a snapshot of all tasks; we
 * match by TaskHandle_t and compute (task_delta / total_delta * 100) for the
 * five tracked tasks. The first call after boot has no prev snapshot, so it
 * just seeds prev_runtime_counter; percentages remain 0 until the next call.
 */
static void collect_cpu_usage(void) {
    refresh_task_handles();

    // FreeRTOS exposes uxTaskGetNumberOfTasks() to size the buffer. Add headroom
    // so a task created between sizing and snapshot doesn't truncate.
    UBaseType_t n = uxTaskGetNumberOfTasks() + 4;
    TaskStatus_t* arr = (TaskStatus_t*)malloc(n * sizeof(TaskStatus_t));
    if (arr == NULL) {
        // Not fatal — CPU% just stays at last value.
        return;
    }
    uint32_t total_runtime = 0;
    UBaseType_t got = uxTaskGetSystemState(arr, n, &total_runtime);

    // Compute total runtime delta for the period.
    // total_runtime is a free-running counter; on the first call we record it
    // and bail out (percentages stay 0 until the next call).
    if (prev_total_runtime == 0) {
        prev_total_runtime = total_runtime;
        for (UBaseType_t i = 0; i < got; i++) {
            for (int t = 0; t < TRACKED_TASK_COUNT; t++) {
                if (arr[i].xHandle == cpu_state[t].handle) {
                    cpu_state[t].prev_runtime_counter = arr[i].ulRunTimeCounter;
                }
            }
        }
        free(arr);
        return;
    }

    uint32_t total_delta = total_runtime - prev_total_runtime;
    prev_total_runtime = total_runtime;
    if (total_delta == 0) {
        free(arr);
        return;
    }

    // Walk every task in the snapshot, find each tracked handle, compute %.
    for (int t = 0; t < TRACKED_TASK_COUNT; t++) {
        if (cpu_state[t].handle == NULL) {
            stats.period.cpu_usage_percent[t] = 0.0f;
            continue;
        }
        for (UBaseType_t i = 0; i < got; i++) {
            if (arr[i].xHandle == cpu_state[t].handle) {
                uint32_t task_delta = arr[i].ulRunTimeCounter - cpu_state[t].prev_runtime_counter;
                cpu_state[t].prev_runtime_counter = arr[i].ulRunTimeCounter;
                // Multiply first to avoid losing precision; total_delta is bounded.
                stats.period.cpu_usage_percent[t] =
                    (float)task_delta * 100.0f / (float)total_delta;
                break;
            }
        }
    }
    free(arr);
}

/**
 * @brief Query per-task loop-time getters and write avg into the period struct.
 */
static void collect_loop_times(void) {
    stats.period.avg_task_loop_time_ms[TASK_IDX_NTRIP]   = ntrip_task_get_avg_loop_us_and_reset() / 1000;
    stats.period.avg_task_loop_time_ms[TASK_IDX_GNSS]    = gnss_task_get_avg_loop_us_and_reset() / 1000;
    stats.period.avg_task_loop_time_ms[TASK_IDX_DATAOUT] = data_output_task_get_avg_loop_us_and_reset() / 1000;
    stats.period.avg_task_loop_time_ms[TASK_IDX_LED]     = led_task_get_avg_loop_us_and_reset() / 1000;
    // Stats task self-measurement
    uint32_t self_avg_us = (stats_loop_count > 0)
        ? (uint32_t)(stats_loop_time_sum_us / stats_loop_count)
        : 0;
    stats.period.avg_task_loop_time_ms[TASK_IDX_STATS] = self_avg_us / 1000;
    stats_loop_time_sum_us = 0;
    stats_loop_count = 0;
}

/**
 * @brief Convert WGS84 latitude/longitude/altitude to ECEF coordinates
 * @param lat Latitude in decimal degrees
 * @param lon Longitude in decimal degrees
 * @param alt Altitude in meters (above WGS84 ellipsoid)
 * @param x Output ECEF X coordinate (meters)
 * @param y Output ECEF Y coordinate (meters)
 * @param z Output ECEF Z coordinate (meters)
 */
static void lat_lon_alt_to_ecef(double lat, double lon, float alt, double* x, double* y, double* z) {
    // WGS84 constants
    const double a = 6378137.0;              // Semi-major axis (m)
    const double e2 = 0.00669437999014;      // First eccentricity squared
    
    // Convert degrees to radians
    double lat_rad = lat * M_PI / 180.0;
    double lon_rad = lon * M_PI / 180.0;
    
    // Calculate prime vertical radius of curvature (N)
    double sin_lat = sin(lat_rad);
    double cos_lat = cos(lat_rad);
    double N = a / sqrt(1.0 - e2 * sin_lat * sin_lat);
    
    // Calculate ECEF coordinates
    *x = (N + alt) * cos_lat * cos(lon_rad);
    *y = (N + alt) * cos_lat * sin(lon_rad);
    *z = (N * (1.0 - e2) + alt) * sin_lat;
}

/**
 * @brief Collect WiFi statistics
 */
static void collect_wifi_stats(void) {
    // Check WiFi connection status
    bool wifi_connected = wifi_manager_is_sta_connected();
    
    if (wifi_connected) {
        stats.runtime.wifi_uptime_sec++;
        stats.period.wifi_uptime_sec++;
        
        // Get RSSI
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            int8_t rssi = ap_info.rssi;
            
            // Update period
            stats.period.wifi_rssi_dbm = rssi;
            if (rssi < stats.period.wifi_rssi_min) {
                stats.period.wifi_rssi_min = rssi;
            }
            if (rssi > stats.period.wifi_rssi_max) {
                stats.period.wifi_rssi_max = rssi;
            }
            rssi_sum += rssi;
            rssi_sample_count++;
            stats.period.wifi_rssi_avg = rssi_sample_count > 0 ? (rssi_sum / rssi_sample_count) : 0;
            
            // Update runtime
            if (rssi < stats.runtime.wifi_rssi_min_boot) {
                stats.runtime.wifi_rssi_min_boot = rssi;
            }
            if (rssi > stats.runtime.wifi_rssi_max_boot) {
                stats.runtime.wifi_rssi_max_boot = rssi;
            }
        }
    }
    
    // Calculate period uptime percentage using monotonic uptime delta —
    // not wall-clock subtraction (which mixed time bases in the old code).
    uint32_t period_elapsed = stats.runtime.system_uptime_sec - stats.period_start_uptime_sec;
    if (period_elapsed > 0) {
        stats.period.wifi_uptime_percent = (float)stats.period.wifi_uptime_sec * 100.0f / period_elapsed;
        if (stats.period.wifi_uptime_percent > 100.0f) stats.period.wifi_uptime_percent = 100.0f;
    }
}

/**
 * @brief Collect GNSS statistics
 */
static void collect_gnss_stats(void) {
    gnss_data_t gnss_data;
    gnss_get_data(&gnss_data);
    
    // Get GNSS update rate (Hz)
    stats.period.gnss_update_rate_hz = gnss_get_update_count_and_reset();
    
    if (gnss_data.valid) {
        // Track fix quality changes
        if (gnss_data.fix_quality != last_fix_quality) {
            // Increment appropriate counter
            if (gnss_data.fix_quality < last_fix_quality) {
                stats.runtime.fix_downgrades_total++;
                stats.period.fix_downgrades++;
            } else if (gnss_data.fix_quality > last_fix_quality) {
                stats.runtime.fix_upgrades_total++;
                stats.period.fix_upgrades++;
            }
            
            // Update timestamps for fix progression
            struct timeval tv;
            gettimeofday(&tv, NULL);
            
            if (last_fix_quality == 0 && gnss_data.fix_quality >= 1) {
                // First fix achieved
                if (stats.runtime.time_to_first_fix_sec == 0) {
                    stats.runtime.time_to_first_fix_sec = stats.runtime.system_uptime_sec;
                }
            }
            if (last_fix_quality < 5 && gnss_data.fix_quality == 5) {
                // RTK float achieved
                if (stats.runtime.time_to_rtk_float_sec == 0) {
                    stats.runtime.time_to_rtk_float_sec = stats.runtime.system_uptime_sec;
                }
            }
            if (last_fix_quality != 4 && gnss_data.fix_quality == 4) {
                // RTK fixed achieved
                if (stats.runtime.time_to_rtk_fixed_sec == 0) {
                    stats.runtime.time_to_rtk_fixed_sec = stats.runtime.system_uptime_sec;
                }
            }
            
            last_fix_quality = gnss_data.fix_quality;
            last_fix_quality_change = tv.tv_sec;
        }
        
        // Update fix quality duration
        if (gnss_data.fix_quality < 9) {
            stats.runtime.fix_quality_duration_total[gnss_data.fix_quality]++;
            stats.period.fix_quality_duration[gnss_data.fix_quality]++;
        }
        
        // Calculate current fix duration
        struct timeval tv;
        gettimeofday(&tv, NULL);
        stats.runtime.current_fix_duration_sec = tv.tv_sec - last_fix_quality_change;
        
        // Calculate RTK fixed stability (period only) using monotonic uptime delta.
        // Old code mixed wall-clock UNIX seconds with seconds-since-boot, producing
        // a denominator that drifted upwards across periods and made the percentage
        // appear to shrink even while the device stayed locked at 100% RTK Fixed.
        uint32_t period_elapsed = stats.runtime.system_uptime_sec - stats.period_start_uptime_sec;
        if (period_elapsed > 0) {
            stats.period.rtk_fixed_stability_percent =
                (float)stats.period.fix_quality_duration[4] * 100.0f / period_elapsed;
            if (stats.period.rtk_fixed_stability_percent > 100.0f) {
                stats.period.rtk_fixed_stability_percent = 100.0f;
            }
        }
        
        // Update HDOP statistics
        if (gnss_data.hdop > 0.0f) {
            stats.period.hdop_current = gnss_data.hdop;
            if (gnss_data.hdop < stats.period.hdop_min) {
                stats.period.hdop_min = gnss_data.hdop;
            }
            if (gnss_data.hdop > stats.period.hdop_max) {
                stats.period.hdop_max = gnss_data.hdop;
            }
            hdop_sum += gnss_data.hdop;
            hdop_sample_count++;
            stats.period.hdop_avg = hdop_sum / hdop_sample_count;
            
            // Runtime HDOP
            if (gnss_data.hdop < stats.runtime.hdop_min_boot) {
                stats.runtime.hdop_min_boot = gnss_data.hdop;
            }
            if (gnss_data.hdop > stats.runtime.hdop_max_boot) {
                stats.runtime.hdop_max_boot = gnss_data.hdop;
            }
            
            // Calculate estimated accuracy based on fix type
            float uere = 7.0f; // Default GPS
            switch (gnss_data.fix_quality) {
                case 2: uere = 3.0f; break;   // DGPS
                case 5: uere = 0.5f; break;   // RTK Float
                case 4: uere = 0.02f; break;  // RTK Fixed
            }
            stats.period.estimated_accuracy_m = gnss_data.hdop * uere;
        }
        
        // Update satellite statistics
        if (gnss_data.satellites > 0) {
            stats.period.satellites_current = gnss_data.satellites;
            if (gnss_data.satellites < stats.period.satellites_min) {
                stats.period.satellites_min = gnss_data.satellites;
            }
            if (gnss_data.satellites > stats.period.satellites_max) {
                stats.period.satellites_max = gnss_data.satellites;
            }
            sat_sum += gnss_data.satellites;
            sat_sample_count++;
            stats.period.satellites_avg = sat_sample_count > 0 ? (sat_sum / sat_sample_count) : 0;
            
            // Runtime satellites
            if (gnss_data.satellites < stats.runtime.satellites_min_boot) {
                stats.runtime.satellites_min_boot = gnss_data.satellites;
            }
            if (gnss_data.satellites > stats.runtime.satellites_max_boot) {
                stats.runtime.satellites_max_boot = gnss_data.satellites;
            }
        }
    }
    
    // Calculate baseline distance if we have both rover and reference station positions
    if (gnss_data.valid && gnss_data.fix_quality >= 1) {
        double ref_x, ref_y, ref_z;
        if (ntrip_get_reference_station_ecef(&ref_x, &ref_y, &ref_z)) {
            // Convert rover position from lat/lon/alt to ECEF
            double rover_x, rover_y, rover_z;
            lat_lon_alt_to_ecef(gnss_data.latitude, gnss_data.longitude, gnss_data.altitude, 
                               &rover_x, &rover_y, &rover_z);
            
            // Calculate Euclidean distance
            double dx = rover_x - ref_x;
            double dy = rover_y - ref_y;
            double dz = rover_z - ref_z;
            double distance_m = sqrt(dx * dx + dy * dy + dz * dz);
            
            // Convert to kilometers
            stats.period.baseline_distance_km = (float)(distance_m / 1000.0);
            
            // Sanity check: baseline distance should typically be < 200 km for RTK
            if (stats.period.baseline_distance_km > 200.0) {
                ESP_LOGW(TAG, "WARNING: Baseline distance %.2f km exceeds typical RTK range (< 200 km)", 
                         stats.period.baseline_distance_km);
                // Check if coordinates look reasonable (should be ~6.4 million meters from Earth center)
                double rover_radius = sqrt(rover_x * rover_x + rover_y * rover_y + rover_z * rover_z);
                double ref_radius = sqrt(ref_x * ref_x + ref_y * ref_y + ref_z * ref_z);
                
                if (rover_radius < 100000.0 || rover_radius > 10000000.0) {
                    ESP_LOGE(TAG, "ERROR: Rover ECEF invalid (radius %.0f m). Check NMEA parsing.", rover_radius);
                }
                if (ref_radius < 100000.0 || ref_radius > 10000000.0) {
                    ESP_LOGE(TAG, "ERROR: Reference ECEF invalid (radius %.0f m). Check RTCM 1005 parsing.", ref_radius);
                }
            }
        }
    }
}

/**
 * @brief Log statistics summary
 */
static void log_statistics_summary(void) {
    ESP_LOGI(TAG, "=== Statistics Summary (Period: %lu sec) ===", stats.period_duration_sec);
    ESP_LOGI(TAG, "System: Uptime=%lu sec, Heap Free=%lu bytes, Min Heap=%lu bytes",
             stats.runtime.system_uptime_sec, stats.period.heap_free_bytes, stats.runtime.heap_min_free_bytes);
    ESP_LOGI(TAG, "GNSS: Fix=%d, HDOP=%.2f, Sats=%d, Accuracy=%.3fm",
             last_fix_quality, stats.period.hdop_current, stats.period.satellites_current, 
             stats.period.estimated_accuracy_m);
    ESP_LOGI(TAG, "RTK Fixed: %.1f%% (period), %lu sec (total)",
             stats.period.rtk_fixed_stability_percent, stats.runtime.fix_quality_duration_total[4]);
    ESP_LOGI(TAG, "RTCM: %lu bytes (%lu B/s), %lu msgs (%lu msg/s)",
             stats.period.rtcm_bytes_received, stats.period.rtcm_bytes_per_sec,
             stats.period.rtcm_messages_received, stats.period.rtcm_message_rate);
    ESP_LOGI(TAG, "WiFi: Connected %.1f%%, RSSI=%d dBm (avg=%d)",
             stats.period.wifi_uptime_percent, stats.period.wifi_rssi_dbm, stats.period.wifi_rssi_avg);
    ESP_LOGI(TAG, "GGA: Sent=%lu, Failures=%lu (period)",
             stats.period.gga_sent_count, stats.period.gga_send_failures);
    ESP_LOGI(TAG, "Errors: NMEA=%lu, UART=%lu, NTRIP timeouts=%lu (period)",
             stats.period.nmea_checksum_errors, stats.period.uart_errors, stats.period.ntrip_timeouts);
}

/**
 * @brief Statistics task main function
 */
static void statistics_task(void *pvParameters) {
    (void)pvParameters;
    
    uint32_t log_counter = 0;
    
    ESP_LOGI(TAG, "Statistics Task started (interval: %lu sec)", config.interval_sec);
    
    while (1) {
        int64_t loop_start_us = esp_timer_get_time();
        if (config.enabled && xSemaphoreTake(stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Update uptime
            update_uptime();

            // Collect all statistics
            collect_heap_stats();
            collect_stack_hwm();
            collect_wifi_stats();
            collect_gnss_stats();
            collect_queue_stats();

            xSemaphoreGive(stats_mutex);

            // Check if log interval elapsed
            log_counter++;
            if (log_counter >= config.interval_sec) {
                // Calculate period rates using the just-elapsed duration. For the
                // very first log we don't have a previous period_duration_sec, so
                // fall back to log_counter (which equals interval_sec by definition here).
                if (xSemaphoreTake(stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    uint32_t uptime_now = (uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ);
                    uint32_t period_sec = uptime_now - stats.period_start_uptime_sec;
                    if (period_sec == 0) period_sec = log_counter;

                    stats.period.rtcm_bytes_per_sec = stats.period.rtcm_bytes_received / period_sec;
                    stats.period.rtcm_message_rate  = stats.period.rtcm_messages_received / period_sec;

                    if (rtcm_latency_count > 0) {
                        stats.period.rtcm_avg_latency_ms = (uint32_t)(rtcm_latency_sum_ms / rtcm_latency_count);
                    }

                    // Queue averages: sum-of-samples / number-of-samples
                    if (queue_depth_sample_count > 0) {
                        stats.period.rtcm_queue_avg_count =
                            (uint32_t)(rtcm_queue_depth_sum / queue_depth_sample_count);
                        stats.period.gga_queue_avg_count =
                            (uint32_t)(gga_queue_depth_sum / queue_depth_sample_count);
                    }

                    // Telemetry output rate (Hz) — count of transmitted frames / period
                    stats.period.telemetry_output_rate_hz =
                        data_output_get_tx_count_and_reset() / period_sec;

                    // CPU usage % per tracked task and average per-task loop times.
                    collect_cpu_usage();
                    collect_loop_times();

                    // Make the in-struct "period_duration_sec" reflect the period we
                    // are about to LOG (so the log header isn't 0 on the first pass
                    // or stale afterwards).
                    stats.period_duration_sec = period_sec;

                    xSemaphoreGive(stats_mutex);
                }

                // Log summary
                log_statistics_summary();

                // Reset period statistics
                reset_period_stats();
                log_counter = 0;
            }
        }

        // Self-measure loop time
        int64_t loop_end_us = esp_timer_get_time();
        stats_loop_time_sum_us += (uint64_t)(loop_end_us - loop_start_us);
        stats_loop_count++;

        vTaskDelay(pdMS_TO_TICKS(STATS_UPDATE_RATE_MS));
    }
}

/**
 * @brief Initialize the Statistics Task
 */
void statistics_task_init(void) {
    // Create mutex
    if (stats_mutex == NULL) {
        stats_mutex = xSemaphoreCreateMutex();
        if (stats_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create statistics mutex");
            return;
        }
    }
    
    // Initialize statistics
    init_statistics();
    
    // Create task
    BaseType_t result = xTaskCreate(
        statistics_task,
        "statistics",
        STATS_TASK_STACK_SIZE,
        NULL,
        STATS_TASK_PRIORITY,
        &stats_task_handle
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Statistics Task");
    } else {
        ESP_LOGI(TAG, "Statistics Task initialized");
    }
}

/**
 * @brief Get current statistics (thread-safe)
 */
void statistics_get(system_statistics_t* out_stats) {
    if (out_stats != NULL && xSemaphoreTake(stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(out_stats, &stats, sizeof(system_statistics_t));
        xSemaphoreGive(stats_mutex);
    }
}

/**
 * @brief Get runtime statistics only (thread-safe)
 */
void statistics_get_runtime(runtime_statistics_t* out_stats) {
    if (out_stats != NULL && xSemaphoreTake(stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(out_stats, &stats.runtime, sizeof(runtime_statistics_t));
        xSemaphoreGive(stats_mutex);
    }
}

/**
 * @brief Get period statistics only (thread-safe)
 */
void statistics_get_period(period_statistics_t* out_stats) {
    if (out_stats != NULL && xSemaphoreTake(stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Copy period stats
        memcpy(out_stats, &stats.period, sizeof(period_statistics_t));

        // Calculate rates on-the-fly if period duration is available
        struct timeval tv;
        gettimeofday(&tv, NULL);
        uint32_t period_sec = tv.tv_sec - stats.period_start_time;

        if (period_sec > 0) {
            out_stats->rtcm_bytes_per_sec = stats.period.rtcm_bytes_received / period_sec;
            out_stats->rtcm_message_rate = stats.period.rtcm_messages_received / period_sec;
        }

        xSemaphoreGive(stats_mutex);
    }
}

// Classify a single dimension into the worst-matching quality bucket. Higher
// enum value = worse — caller takes max() across dimensions.
static network_quality_t classify_rssi(int8_t rssi) {
    // rssi == 0 means "no samples yet" (period just reset / WiFi not up).
    // Treat as EXCELLENT so a fresh period doesn't immediately stretch intervals.
    if (rssi == 0)         return NETWORK_QUALITY_EXCELLENT;
    if (rssi >= -60)       return NETWORK_QUALITY_EXCELLENT;
    if (rssi >= -70)       return NETWORK_QUALITY_GOOD;
    if (rssi >= -80)       return NETWORK_QUALITY_DEGRADED;
    if (rssi >= -85)       return NETWORK_QUALITY_POOR;
    return NETWORK_QUALITY_CRITICAL;
}

static network_quality_t classify_count(uint32_t count) {
    if (count == 0) return NETWORK_QUALITY_EXCELLENT;
    if (count == 1) return NETWORK_QUALITY_DEGRADED;
    if (count == 2) return NETWORK_QUALITY_POOR;
    return NETWORK_QUALITY_CRITICAL;
}

extern "C" network_quality_t network_quality_classify(void) {
    period_statistics_t p;
    statistics_get_period(&p);

    network_quality_t worst = classify_rssi(p.wifi_rssi_avg);
    network_quality_t q;
    q = classify_count(p.rtcm_data_gaps);     if (q > worst) worst = q;
    q = classify_count(p.ntrip_timeouts);     if (q > worst) worst = q;
    q = classify_count(p.wifi_reconnect_count); if (q > worst) worst = q;
    return worst;
}

extern "C" uint8_t network_quality_interval_mult_x10(network_quality_t q) {
    switch (q) {
        case NETWORK_QUALITY_EXCELLENT:
        case NETWORK_QUALITY_GOOD:      return 10;  // 1.0×
        case NETWORK_QUALITY_DEGRADED:  return 15;  // 1.5×
        case NETWORK_QUALITY_POOR:
        case NETWORK_QUALITY_CRITICAL:  return 30;  // 3.0×
        default:                        return 10;
    }
}

/**
 * @brief Update RTCM data received counter
 */
void statistics_rtcm_received(uint32_t bytes, uint32_t messages) {
    if (xSemaphoreTake(stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        stats.runtime.rtcm_bytes_received_total += bytes;
        stats.runtime.rtcm_messages_received_total += messages;
        stats.period.rtcm_bytes_received += bytes;
        stats.period.rtcm_messages_received += messages;
        xSemaphoreGive(stats_mutex);
    }
}

/**
 * @brief Update GGA transmission counter
 */
void statistics_gga_sent(bool success) {
    if (xSemaphoreTake(stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (success) {
            stats.runtime.gga_sent_count_total++;
            stats.period.gga_sent_count++;

            struct timeval tv;
            gettimeofday(&tv, NULL);

            // Track actual delta between consecutive successful sends — this
            // is the *observed* interval (which can drift from the configured
            // gga_interval_sec due to task scheduling or queue empties).
            if (prev_gga_sent_time != 0 && tv.tv_sec > prev_gga_sent_time) {
                uint32_t delta = (uint32_t)(tv.tv_sec - prev_gga_sent_time);
                gga_interval_sum_sec += delta;
                gga_interval_count++;
                stats.period.gga_actual_interval_sec =
                    (uint32_t)(gga_interval_sum_sec / gga_interval_count);
            }
            prev_gga_sent_time = tv.tv_sec;
            stats.runtime.last_gga_sent_time = tv.tv_sec;
        } else {
            stats.runtime.gga_send_failures_total++;
            stats.period.gga_send_failures++;
        }
        xSemaphoreGive(stats_mutex);
    }
}

/**
 * @brief Update telemetry JSON receive counter
 */
void statistics_telemetry_received(bool crc_ok) {
    if (xSemaphoreTake(stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (crc_ok) {
            stats.runtime.telemetry_json_received++;
        } else {
            stats.runtime.telemetry_json_crc_fail++;
        }
        xSemaphoreGive(stats_mutex);
    }
}

/**
 * @brief Update NMEA checksum error counter
 */
void statistics_nmea_checksum_error(void) {
    if (xSemaphoreTake(stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        stats.runtime.nmea_checksum_errors_total++;
        stats.period.nmea_checksum_errors++;
        xSemaphoreGive(stats_mutex);
    }
}

/**
 * @brief Update UART error counter
 */
void statistics_uart_error(void) {
    if (xSemaphoreTake(stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        stats.runtime.uart_errors_total++;
        stats.period.uart_errors++;
        xSemaphoreGive(stats_mutex);
    }
}

/**
 * @brief Update RTCM queue overflow counter
 */
void statistics_rtcm_queue_overflow(void) {
    if (xSemaphoreTake(stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        stats.runtime.rtcm_queue_overflows_total++;
        stats.period.rtcm_queue_overflows++;
        xSemaphoreGive(stats_mutex);
    }
}

/**
 * @brief Update GGA queue overflow counter
 */
void statistics_gga_queue_overflow(void) {
    if (xSemaphoreTake(stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        stats.runtime.gga_queue_overflows_total++;
        stats.period.gga_queue_overflows++;
        xSemaphoreGive(stats_mutex);
    }
}

/**
 * @brief Update NTRIP timeout counter
 */
void statistics_ntrip_timeout(void) {
    if (xSemaphoreTake(stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        stats.runtime.ntrip_timeouts_total++;
        stats.period.ntrip_timeouts++;
        xSemaphoreGive(stats_mutex);
    }
}

/**
 * @brief Update RTCM latency measurement
 */
void statistics_rtcm_latency(uint32_t latency_ms) {
    if (xSemaphoreTake(stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        rtcm_latency_sum_ms += latency_ms;
        rtcm_latency_count++;
        xSemaphoreGive(stats_mutex);
    }
}

/**
 * @brief Update RTCM data gap counter
 */
void statistics_rtcm_data_gap(uint32_t gap_sec) {
    if (xSemaphoreTake(stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        stats.runtime.rtcm_data_gaps_total++;
        stats.period.rtcm_data_gaps++;
        stats.period.rtcm_gap_duration_sec += gap_sec;
        xSemaphoreGive(stats_mutex);
    }
}

/**
 * @brief Update NTRIP connection event counter.
 */
void statistics_ntrip_event(ntrip_stats_event_t event) {
    if (xSemaphoreTake(stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        struct timeval tv;
        gettimeofday(&tv, NULL);

        switch (event) {
            case NTRIP_STATS_EVENT_RECONNECT_BEGIN:
                // Stamp the start time so we can measure reconnect duration on CONNECTED.
                ntrip_reconnect_begin_us = esp_timer_get_time();
                break;

            case NTRIP_STATS_EVENT_CONNECTED:
                stats.runtime.last_connection_state_change = tv.tv_sec;
                // If we have a pending reconnect-begin timestamp, this CONNECTED
                // counts as a reconnect (the very first connection won't have one).
                if (ntrip_reconnect_begin_us > 0) {
                    int64_t dur_us = esp_timer_get_time() - ntrip_reconnect_begin_us;
                    if (dur_us > 0) {
                        ntrip_reconnect_time_sum_ms += (uint64_t)(dur_us / 1000);
                        ntrip_reconnect_time_count++;
                        // Running average across the device's lifetime — runtime metric.
                        stats.runtime.ntrip_avg_reconnect_time_ms =
                            (uint32_t)(ntrip_reconnect_time_sum_ms / ntrip_reconnect_time_count);
                    }
                    stats.runtime.ntrip_reconnect_count++;
                    ntrip_reconnect_begin_us = 0;
                }
                break;

            case NTRIP_STATS_EVENT_DISCONNECTED:
                stats.runtime.last_connection_state_change = tv.tv_sec;
                break;
        }
        xSemaphoreGive(stats_mutex);
    }
}

/**
 * @brief Increment the WiFi reconnect counter.
 */
void statistics_wifi_reconnect(void) {
    if (xSemaphoreTake(stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        stats.runtime.wifi_reconnect_count_total++;
        stats.period.wifi_reconnect_count++;
        xSemaphoreGive(stats_mutex);
    }
}

void statistics_config_load_failure(void) {
    if (xSemaphoreTake(stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        stats.runtime.config_load_failures_total++;
        xSemaphoreGive(stats_mutex);
    }
}

void statistics_task_creation_failure(void) {
    if (xSemaphoreTake(stats_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        stats.runtime.task_creation_failures_total++;
        xSemaphoreGive(stats_mutex);
    }
}

