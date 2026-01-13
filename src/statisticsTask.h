/**
 * @file statisticsTask.h
 * @brief Statistics Task header for system performance monitoring
 * 
 * This module tracks and aggregates system performance metrics and operational
 * statistics for RTK/NTRIP client operation, GPS fix quality, and overall system health.
 * 
 * Statistics are split into two categories:
 * - Runtime: Cumulative from boot, total lifetime statistics
 * - Period: For the duration of the current log interval only
 * 
 * All statistics are held in RAM and reset on system reboot.
 */

#ifndef STATISTICS_TASK_H
#define STATISTICS_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/**
 * @brief Configuration structure for statistics collection.
 */
typedef struct {
    uint32_t interval_sec;        /**< Logging interval in seconds */
    bool enabled;                 /**< Enable/disable statistics collection */
    bool web_api_enable;          /**< Enable HTTP API for statistics */
    bool mqtt_publish;            /**< Publish statistics via MQTT */
} statistics_config_t;

/**
 * @brief Runtime statistics - cumulative from boot.
 */
typedef struct {
    // NTRIP metrics [Runtime]
    uint32_t ntrip_uptime_sec;                /**< NTRIP uptime in seconds */
    uint32_t ntrip_reconnect_count;           /**< Number of NTRIP reconnects */
    uint32_t ntrip_avg_reconnect_time_ms;     /**< Average NTRIP reconnect time (ms) */
    uint32_t ntrip_auth_failures;             /**< NTRIP authentication failures */
    time_t last_connection_state_change;      /**< Last NTRIP connection state change timestamp */
    // RTCM metrics [Runtime]
    uint64_t rtcm_bytes_received_total;       /**< Total RTCM bytes received */
    uint32_t rtcm_messages_received_total;    /**< Total RTCM messages received */
    uint32_t rtcm_data_gaps_total;            /**< Total RTCM data gaps */
    uint32_t rtcm_corrupted_count_total;      /**< Total RTCM corrupted messages */
    uint32_t rtcm_queue_overflows_total;      /**< Total RTCM queue overflows */
    // GPS fix metrics [Runtime]
    uint32_t time_to_first_fix_sec;           /**< Time to first GPS fix (sec) */
    uint32_t time_to_rtk_float_sec;           /**< Time to RTK float (sec) */
    uint32_t time_to_rtk_fixed_sec;           /**< Time to RTK fixed (sec) */
    uint32_t fix_quality_duration_total[9];   /**< Seconds in each fix quality state */
    uint32_t fix_downgrades_total;            /**< Number of fix downgrades */
    uint32_t fix_upgrades_total;              /**< Number of fix upgrades */
    uint32_t current_fix_duration_sec;        /**< Time in current fix state (sec) */
    // Accuracy metrics [Runtime]
    float hdop_min_boot;                      /**< Minimum HDOP since boot */
    float hdop_max_boot;                      /**< Maximum HDOP since boot */
    uint8_t satellites_min_boot;              /**< Minimum satellites since boot */
    uint8_t satellites_max_boot;              /**< Maximum satellites since boot */
    // GGA transmission [Runtime]
    uint32_t gga_sent_count_total;            /**< Total GGA sentences sent */
    uint32_t gga_send_failures_total;         /**< Total GGA send failures */
    uint32_t gga_queue_overflows_total;       /**< Total GGA queue overflows */
    time_t last_gga_sent_time;                /**< Last GGA sent timestamp */
    // System health [Runtime]
    uint32_t wifi_uptime_sec;                 /**< WiFi uptime in seconds */
    int8_t wifi_rssi_min_boot;                /**< Minimum WiFi RSSI since boot */
    int8_t wifi_rssi_max_boot;                /**< Maximum WiFi RSSI since boot */
    uint32_t wifi_reconnect_count_total;      /**< Total WiFi reconnects */
    uint32_t heap_min_free_bytes;             /**< Minimum free heap bytes */
    uint32_t stack_hwm_ntrip;                 /**< NTRIP task stack high-water mark */
    uint32_t stack_hwm_gnss;                  /**< GNSS task stack high-water mark */
    uint32_t stack_hwm_dataout;               /**< Data output task stack high-water mark */
    uint32_t stack_hwm_stats;                 /**< Statistics task stack high-water mark */
    uint32_t stack_hwm_led;                   /**< LED task stack high-water mark */
    uint32_t system_uptime_sec;               /**< System uptime in seconds */
    uint32_t rtcm_queue_peak_count;           /**< RTCM queue peak count */
    uint32_t gga_queue_peak_count;            /**< GGA queue peak count */
    // Error counters [Runtime]
    uint32_t nmea_checksum_errors_total;      /**< Total NMEA checksum errors */
    uint32_t uart_errors_total;               /**< Total UART errors */
    uint32_t ntrip_timeouts_total;            /**< Total NTRIP timeouts */
    uint32_t config_load_failures_total;      /**< Total config load failures */
    uint32_t memory_alloc_failures_total;     /**< Total memory allocation failures */
    uint32_t task_creation_failures_total;    /**< Total task creation failures */
} runtime_statistics_t;

/**
 * @brief Period statistics - for current log interval only.
 */
typedef struct {
    // RTCM metrics [Period]
    uint32_t rtcm_bytes_received;          /**< RTCM bytes received this period */
    uint32_t rtcm_bytes_per_sec;           /**< RTCM bytes per second */
    uint32_t rtcm_messages_received;       /**< RTCM messages received this period */
    uint32_t rtcm_message_rate;            /**< RTCM message rate (messages/sec) */
    uint32_t rtcm_avg_latency_ms;          /**< Average RTCM latency (ms) */
    uint32_t rtcm_data_gaps;               /**< RTCM data gaps this period */
    uint32_t rtcm_gap_duration_sec;        /**< RTCM gap duration (sec) */
    uint32_t rtcm_corrupted_count;         /**< RTCM corrupted messages this period */
    uint32_t rtcm_queue_overflows;         /**< RTCM queue overflows this period */
    // GPS fix metrics [Period]
    uint32_t fix_quality_duration[9];      /**< Seconds in each fix quality state this period */
    float rtk_fixed_stability_percent;     /**< RTK fixed stability percent */
    uint32_t fix_downgrades;               /**< Number of fix downgrades this period */
    uint32_t fix_upgrades;                 /**< Number of fix upgrades this period */
    // Accuracy metrics [Period]
    float hdop_current;                    /**< Current HDOP */
    float hdop_min;                        /**< Minimum HDOP this period */
    float hdop_max;                        /**< Maximum HDOP this period */
    float hdop_avg;                        /**< Average HDOP this period */
    float estimated_accuracy_m;             /**< Estimated accuracy in meters */
    uint8_t satellites_current;            /**< Current satellites */
    uint8_t satellites_min;                /**< Minimum satellites this period */
    uint8_t satellites_max;                /**< Maximum satellites this period */
    uint8_t satellites_avg;                /**< Average satellites this period */
    float baseline_distance_km;            /**< Baseline distance in km */
    // GGA transmission [Period]
    uint32_t gga_sent_count;               /**< GGA sentences sent this period */
    uint32_t gga_send_failures;            /**< GGA send failures this period */
    uint32_t gga_actual_interval_sec;      /**< Actual GGA interval (sec) */
    uint32_t gga_queue_overflows;          /**< GGA queue overflows this period */
    // System health [Period]
    uint32_t wifi_uptime_sec;              /**< WiFi uptime this period (sec) */
    float wifi_uptime_percent;             /**< WiFi uptime percent this period */
    int8_t wifi_rssi_dbm;                  /**< Current WiFi RSSI (dBm) */
    int8_t wifi_rssi_min;                  /**< Minimum WiFi RSSI this period */
    int8_t wifi_rssi_max;                  /**< Maximum WiFi RSSI this period */
    int8_t wifi_rssi_avg;                  /**< Average WiFi RSSI this period */
    uint32_t wifi_reconnect_count;         /**< WiFi reconnects this period */
    uint32_t heap_free_bytes;              /**< Free heap bytes this period */
    uint32_t heap_largest_block;           /**< Largest heap block this period */
    float cpu_usage_percent[5];            /**< CPU usage percent per task */
    // Error counters [Period]
    uint32_t nmea_checksum_errors;         /**< NMEA checksum errors this period */
    uint32_t uart_errors;                  /**< UART errors this period */
    uint32_t ntrip_timeouts;               /**< NTRIP timeouts this period */
    // Performance metrics [Period]
    uint32_t gnss_update_rate_hz;          /**< GNSS update rate (Hz) */
    uint32_t telemetry_output_rate_hz;     /**< Telemetry output rate (Hz) */
    uint32_t avg_task_loop_time_ms[5];     /**< Average task loop time (ms) */
    uint32_t event_latency_ms;             /**< Event latency (ms) */
    uint32_t rtcm_queue_avg_count;         /**< RTCM queue average count */
    uint32_t gga_queue_avg_count;          /**< GGA queue average count */
} period_statistics_t;

/**
 * @brief Combined statistics structure (runtime + period).
 */
typedef struct {
    runtime_statistics_t runtime;        /**< Runtime statistics */
    period_statistics_t period;          /**< Period statistics */
    time_t period_start_time;            /**< Timestamp when current period started */
    uint32_t period_duration_sec;        /**< Actual duration of completed period */
} system_statistics_t;

/**
 * @brief Initialize the Statistics Task
 * 
 * Creates the statistics task and initializes all counters to zero.
 * Task starts collecting metrics immediately.
 */
void statistics_task_init(void);

/**
 * @brief Stop the Statistics Task
 * 
 * Stops the statistics task and cleans up resources.
 */
void statistics_task_stop(void);

/**
 * @brief Get current statistics (thread-safe)
 * 
 * @param stats Pointer to structure to receive statistics copy
 */
void statistics_get(system_statistics_t* stats);

/**
 * @brief Get runtime statistics only (thread-safe)
 * 
 * @param stats Pointer to structure to receive runtime statistics copy
 */
void statistics_get_runtime(runtime_statistics_t* stats);

/**
 * @brief Get period statistics only (thread-safe)
 * 
 * @param stats Pointer to structure to receive period statistics copy
 */
void statistics_get_period(period_statistics_t* stats);

/**
 * @brief Reset period statistics
 * 
 * Resets all period counters to zero, starts new interval.
 * Runtime statistics are preserved.
 */
void statistics_reset_period(void);

/**
 * @brief Update NTRIP event counter (called by NTRIP task)
 * 
 * @param event_type Event type (connect, disconnect, auth_fail, etc.)
 */
void statistics_ntrip_event(uint8_t event_type);

/**
 * @brief Update RTCM data received counter (called by NTRIP/GNSS tasks)
 * 
 * @param bytes Number of bytes received
 * @param messages Number of messages received
 */
void statistics_rtcm_received(uint32_t bytes, uint32_t messages);

/**
 * @brief Update GPS fix quality event (called by GNSS task)
 * 
 * @param new_quality New fix quality value (0-8)
 */
void statistics_fix_quality_changed(uint8_t new_quality);

/**
 * @brief Update GGA transmission counter (called by NTRIP task)
 * 
 * @param success true if sent successfully, false if failed
 */
void statistics_gga_sent(bool success);

/**
 * @brief Format statistics as JSON string
 * 
 * @param buffer Output buffer for JSON string
 * @param buffer_size Size of output buffer
 * @return Length of JSON string, or -1 on error
 */
int statistics_format_json(char* buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif // STATISTICS_TASK_H
