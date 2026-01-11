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

// Configuration structure
typedef struct {
    uint32_t interval_sec;        // Logging interval in seconds
    bool enabled;                 // Enable/disable statistics collection
    bool web_api_enable;          // Enable HTTP API for statistics
    bool mqtt_publish;            // Publish statistics via MQTT
} statistics_config_t;

// Runtime statistics - cumulative from boot
typedef struct {
    // NTRIP metrics [Runtime]
    uint32_t ntrip_uptime_sec;
    uint32_t ntrip_reconnect_count;
    uint32_t ntrip_avg_reconnect_time_ms;
    uint32_t ntrip_auth_failures;
    time_t last_connection_state_change;
    
    // RTCM metrics [Runtime]
    uint64_t rtcm_bytes_received_total;
    uint32_t rtcm_messages_received_total;
    uint32_t rtcm_data_gaps_total;
    uint32_t rtcm_corrupted_count_total;
    uint32_t rtcm_queue_overflows_total;
    
    // GPS fix metrics [Runtime]
    uint32_t time_to_first_fix_sec;
    uint32_t time_to_rtk_float_sec;
    uint32_t time_to_rtk_fixed_sec;
    uint32_t fix_quality_duration_total[9];  // Seconds in each fix quality state
    uint32_t fix_downgrades_total;
    uint32_t fix_upgrades_total;
    uint32_t current_fix_duration_sec;       // Time in current state
    
    // Accuracy metrics [Runtime]
    float hdop_min_boot;
    float hdop_max_boot;
    uint8_t satellites_min_boot;
    uint8_t satellites_max_boot;
    
    // GGA transmission [Runtime]
    uint32_t gga_sent_count_total;
    uint32_t gga_send_failures_total;
    uint32_t gga_queue_overflows_total;
    time_t last_gga_sent_time;
    
    // System health [Runtime]
    uint32_t wifi_uptime_sec;
    int8_t wifi_rssi_min_boot;
    int8_t wifi_rssi_max_boot;
    uint32_t wifi_reconnect_count_total;
    uint32_t heap_min_free_bytes;
    uint32_t stack_hwm_ntrip;
    uint32_t stack_hwm_gnss;
    uint32_t stack_hwm_dataout;
    uint32_t stack_hwm_stats;
    uint32_t stack_hwm_led;
    uint32_t system_uptime_sec;
    uint32_t rtcm_queue_peak_count;
    uint32_t gga_queue_peak_count;
    
    // Error counters [Runtime]
    uint32_t nmea_checksum_errors_total;
    uint32_t uart_errors_total;
    uint32_t ntrip_timeouts_total;
    uint32_t config_load_failures_total;
    uint32_t memory_alloc_failures_total;
    uint32_t task_creation_failures_total;
} runtime_statistics_t;

// Period statistics - for current log interval only
typedef struct {
    // RTCM metrics [Period]
    uint32_t rtcm_bytes_received;
    uint32_t rtcm_bytes_per_sec;
    uint32_t rtcm_messages_received;
    uint32_t rtcm_message_rate;           // messages/sec
    uint32_t rtcm_avg_latency_ms;
    uint32_t rtcm_data_gaps;
    uint32_t rtcm_gap_duration_sec;
    uint32_t rtcm_corrupted_count;
    uint32_t rtcm_queue_overflows;
    
    // GPS fix metrics [Period]
    uint32_t fix_quality_duration[9];     // Seconds in each state this period
    float rtk_fixed_stability_percent;
    uint32_t fix_downgrades;
    uint32_t fix_upgrades;
    
    // Accuracy metrics [Period]
    float hdop_current;
    float hdop_min;
    float hdop_max;
    float hdop_avg;
    float estimated_accuracy_m;
    uint8_t satellites_current;
    uint8_t satellites_min;
    uint8_t satellites_max;
    uint8_t satellites_avg;
    float baseline_distance_km;
    
    // GGA transmission [Period]
    uint32_t gga_sent_count;
    uint32_t gga_send_failures;
    uint32_t gga_actual_interval_sec;
    uint32_t gga_queue_overflows;
    
    // System health [Period]
    uint32_t wifi_uptime_sec;
    float wifi_uptime_percent;
    int8_t wifi_rssi_dbm;
    int8_t wifi_rssi_min;
    int8_t wifi_rssi_max;
    int8_t wifi_rssi_avg;
    uint32_t wifi_reconnect_count;
    uint32_t heap_free_bytes;
    uint32_t heap_largest_block;
    float cpu_usage_percent[5];           // Per task if available
    
    // Error counters [Period]
    uint32_t nmea_checksum_errors;
    uint32_t uart_errors;
    uint32_t ntrip_timeouts;
    
    // Performance metrics [Period]
    uint32_t gnss_update_rate_hz;
    uint32_t telemetry_output_rate_hz;
    uint32_t avg_task_loop_time_ms[5];
    uint32_t event_latency_ms;
    uint32_t rtcm_queue_avg_count;
    uint32_t gga_queue_avg_count;
} period_statistics_t;

// Combined statistics structure
typedef struct {
    runtime_statistics_t runtime;
    period_statistics_t period;
    time_t period_start_time;            // Timestamp when current period started
    uint32_t period_duration_sec;        // Actual duration of completed period
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
