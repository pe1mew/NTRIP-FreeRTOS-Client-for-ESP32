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
#include "wifiManager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <string.h>
#include <sys/time.h>
#include <stdio.h>

static const char *TAG = "StatsTask";

// Task configuration
#define STATS_TASK_STACK_SIZE   4096
#define STATS_TASK_PRIORITY     1
#define STATS_UPDATE_RATE_MS    1000

// Task handle
static TaskHandle_t stats_task_handle = NULL;

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

/**
 * @brief Initialize statistics structures to zero
 */
static void init_statistics(void) {
    memset(&stats, 0, sizeof(system_statistics_t));
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    stats.period_start_time = tv.tv_sec;
    
    // Initialize min values to max possible
    stats.runtime.hdop_min_boot = 99.9f;
    stats.runtime.satellites_min_boot = 255;
    stats.runtime.wifi_rssi_min_boot = 0;
    stats.runtime.heap_min_free_bytes = 0xFFFFFFFF;
    
    stats.period.hdop_min = 99.9f;
    stats.period.satellites_min = 255;
    stats.period.wifi_rssi_min = 0;
}

/**
 * @brief Reset period statistics
 */
static void reset_period_stats(void) {
    if (xSemaphoreTake(stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Save period start time
        struct timeval tv;
        gettimeofday(&tv, NULL);
        stats.period_duration_sec = tv.tv_sec - stats.period_start_time;
        stats.period_start_time = tv.tv_sec;
        
        // Reset period structure
        memset(&stats.period, 0, sizeof(period_statistics_t));
        
        // Reinitialize min values
        stats.period.hdop_min = 99.9f;
        stats.period.satellites_min = 255;
        stats.period.wifi_rssi_min = 0;
        
        // Reset accumulator variables
        hdop_sample_count = 0;
        hdop_sum = 0.0f;
        sat_sample_count = 0;
        sat_sum = 0;
        rssi_sample_count = 0;
        rssi_sum = 0;
        
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
 * @brief Collect stack high water marks
 * 
 * Note: Stack monitoring requires task handles to be exported from each module.
 * Currently disabled to avoid linker errors. To enable:
 * 1. Make task handles non-static in each task module
 * 2. Declare them in task headers as: extern TaskHandle_t <task>_handle;
 */
static void collect_stack_hwm(void) {
    // Stack monitoring disabled - task handles not exported
    // Future: Add accessor functions in each task module to query stack usage
    
    /*
    // Example implementation when handles are exported:
    extern TaskHandle_t ntrip_task_handle;
    extern TaskHandle_t gnss_task_handle;
    extern TaskHandle_t data_output_task_handle;
    extern TaskHandle_t led_task_handle;
    
    if (ntrip_task_handle != NULL) {
        UBaseType_t hwm = uxTaskGetStackHighWaterMark(ntrip_task_handle);
        if (hwm < stats.runtime.stack_hwm_ntrip || stats.runtime.stack_hwm_ntrip == 0) {
            stats.runtime.stack_hwm_ntrip = hwm;
        }
    }
    */
    
    // Monitor own task stack
    if (stats_task_handle != NULL) {
        UBaseType_t hwm = uxTaskGetStackHighWaterMark(stats_task_handle);
        if (hwm < stats.runtime.stack_hwm_stats || stats.runtime.stack_hwm_stats == 0) {
            stats.runtime.stack_hwm_stats = hwm;
        }
    }
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
            if (rssi < stats.period.wifi_rssi_min || stats.period.wifi_rssi_min == 0) {
                stats.period.wifi_rssi_min = rssi;
            }
            if (rssi > stats.period.wifi_rssi_max) {
                stats.period.wifi_rssi_max = rssi;
            }
            rssi_sum += rssi;
            rssi_sample_count++;
            stats.period.wifi_rssi_avg = rssi_sample_count > 0 ? (rssi_sum / rssi_sample_count) : 0;
            
            // Update runtime
            if (rssi < stats.runtime.wifi_rssi_min_boot || stats.runtime.wifi_rssi_min_boot == 0) {
                stats.runtime.wifi_rssi_min_boot = rssi;
            }
            if (rssi > stats.runtime.wifi_rssi_max_boot) {
                stats.runtime.wifi_rssi_max_boot = rssi;
            }
        }
    }
    
    // Calculate period uptime percentage
    uint32_t period_elapsed = stats.runtime.system_uptime_sec - (stats.period_start_time - 
                              (stats.runtime.system_uptime_sec - stats.period.wifi_uptime_sec));
    if (period_elapsed > 0) {
        stats.period.wifi_uptime_percent = (float)stats.period.wifi_uptime_sec * 100.0f / period_elapsed;
    }
}

/**
 * @brief Collect GNSS statistics
 */
static void collect_gnss_stats(void) {
    gnss_data_t gnss_data;
    gnss_get_data(&gnss_data);
    
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
        
        // Calculate RTK fixed stability (period only)
        uint32_t period_elapsed = stats.runtime.system_uptime_sec - (stats.period_start_time - 
                                  (stats.runtime.system_uptime_sec - stats.period.wifi_uptime_sec));
        if (period_elapsed > 0) {
            stats.period.rtk_fixed_stability_percent = 
                (float)stats.period.fix_quality_duration[4] * 100.0f / period_elapsed;
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
        if (config.enabled && xSemaphoreTake(stats_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Update uptime
            update_uptime();
            
            // Collect all statistics
            collect_heap_stats();
            collect_stack_hwm();
            collect_wifi_stats();
            collect_gnss_stats();
            
            xSemaphoreGive(stats_mutex);
            
            // Check if log interval elapsed
            log_counter++;
            if (log_counter >= config.interval_sec) {
                // Calculate period rates
                if (stats.period_duration_sec > 0 || log_counter > 0) {
                    uint32_t period_sec = (stats.period_duration_sec > 0) ? stats.period_duration_sec : log_counter;
                    stats.period.rtcm_bytes_per_sec = stats.period.rtcm_bytes_received / period_sec;
                    stats.period.rtcm_message_rate = stats.period.rtcm_messages_received / period_sec;
                }
                
                // Log summary
                log_statistics_summary();
                
                // Reset period statistics
                reset_period_stats();
                log_counter = 0;
            }
        }
        
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
 * @brief Stop the Statistics Task
 */
void statistics_task_stop(void) {
    if (stats_task_handle != NULL) {
        vTaskDelete(stats_task_handle);
        stats_task_handle = NULL;
        ESP_LOGI(TAG, "Statistics Task stopped");
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

/**
 * @brief Reset period statistics
 */
void statistics_reset_period(void) {
    reset_period_stats();
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
 * @brief Update GPS fix quality event
 */
void statistics_fix_quality_changed(uint8_t new_quality) {
    // Fix quality changes are tracked in collect_gnss_stats()
    // This function provided for explicit notifications if needed
    (void)new_quality;
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
            stats.runtime.last_gga_sent_time = tv.tv_sec;
        } else {
            stats.runtime.gga_send_failures_total++;
            stats.period.gga_send_failures++;
        }
        xSemaphoreGive(stats_mutex);
    }
}

/**
 * @brief Format statistics as JSON string
 */
int statistics_format_json(char* buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        return -1;
    }
    
    system_statistics_t local_stats;
    statistics_get(&local_stats);
    
    int len = snprintf(buffer, buffer_size,
        "{"
        "\"system\":{"
            "\"uptime_sec\":%lu,"
            "\"heap_free\":%lu,"
            "\"heap_min\":%lu"
        "},"
        "\"gnss\":{"
            "\"fix_quality\":%d,"
            "\"accuracy_m\":%.3f,"
            "\"satellites\":%d,"
            "\"hdop\":%.2f,"
            "\"rtk_fixed_percent\":%.1f"
        "},"
        "\"ntrip\":{"
            "\"uptime_sec\":%lu,"
            "\"reconnects\":%lu"
        "},"
        "\"rtcm\":{"
            "\"bytes_total\":%llu,"
            "\"rate_bps\":%lu,"
            "\"messages\":%lu,"
            "\"msg_rate\":%lu"
        "},"
        "\"wifi\":{"
            "\"uptime_percent\":%.1f,"
            "\"rssi_dbm\":%d,"
            "\"reconnects\":%lu"
        "}"
        "}",
        local_stats.runtime.system_uptime_sec,
        local_stats.period.heap_free_bytes,
        local_stats.runtime.heap_min_free_bytes,
        last_fix_quality,
        local_stats.period.estimated_accuracy_m,
        local_stats.period.satellites_current,
        local_stats.period.hdop_current,
        local_stats.period.rtk_fixed_stability_percent,
        local_stats.runtime.ntrip_uptime_sec,
        local_stats.runtime.ntrip_reconnect_count,
        local_stats.runtime.rtcm_bytes_received_total,
        local_stats.period.rtcm_bytes_per_sec,
        local_stats.period.rtcm_messages_received,
        local_stats.period.rtcm_message_rate,
        local_stats.period.wifi_uptime_percent,
        local_stats.period.wifi_rssi_dbm,
        local_stats.runtime.wifi_reconnect_count_total
    );
    
    return (len > 0 && (size_t)len < buffer_size) ? len : -1;
}
