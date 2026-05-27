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
    time_t last_connection_state_change;      /**< Last NTRIP connection state change timestamp */
    // RTCM metrics [Runtime]
    uint64_t rtcm_bytes_received_total;       /**< Total RTCM bytes received */
    uint32_t rtcm_messages_received_total;    /**< Total RTCM messages received */
    uint32_t rtcm_data_gaps_total;            /**< Total RTCM data gaps */
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
    uint32_t task_creation_failures_total;    /**< Total task creation failures */
    // Telemetry JSON forwarding counters [Runtime]
    uint32_t telemetry_json_received;         /**< Total valid telemetry JSON frames received on UART1 RX */
    uint32_t telemetry_json_crc_fail;         /**< Total CRC failures on UART1 RX telemetry frames */
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
    time_t period_start_time;            /**< Wall-clock timestamp when current period started */
    uint32_t period_start_uptime_sec;    /**< system_uptime_sec at the start of the current period (used for period_elapsed math; mixing wall clock with uptime gives nonsense) */
    uint32_t period_duration_sec;        /**< Actual duration of the most recently completed period (seconds) */
} system_statistics_t;

/**
 * @brief NTRIP connection event types tracked by statistics_ntrip_event().
 */
typedef enum {
    NTRIP_STATS_EVENT_CONNECTED = 0,    /**< Successful TCP+NTRIP handshake */
    NTRIP_STATS_EVENT_DISCONNECTED,     /**< Connection closed (read/write error, peer FIN, etc.) */
    NTRIP_STATS_EVENT_RECONNECT_BEGIN,  /**< Reconnect attempt started; pair with CONNECTED for timing */
} ntrip_stats_event_t;

/**
 * @brief Network quality classification derived from period statistics.
 *
 * Used by NTRIP/MQTT tasks to stretch their send/publish intervals when the
 * uplink is unreliable, reducing retransmits and freeing the radio.
 */
typedef enum {
    NETWORK_QUALITY_EXCELLENT = 0,
    NETWORK_QUALITY_GOOD,
    NETWORK_QUALITY_DEGRADED,
    NETWORK_QUALITY_POOR,
    NETWORK_QUALITY_CRITICAL,
} network_quality_t;

/**
 * @brief Classify current network quality from period stats.
 *
 * Combines wifi_rssi_avg, rtcm_data_gaps, ntrip_timeouts and wifi_reconnect_count
 * for the current period; returns the worst of the per-input verdicts.
 */
network_quality_t network_quality_classify(void);

/**
 * @brief Interval multiplier for the given quality, in tenths.
 *
 * 10 = 1.0×, 15 = 1.5×, 30 = 3.0×. Returns 10 for unknown/excellent so callers
 * that forget to gate on quality see no change in cadence.
 *
 *   effective_sec = (configured_sec * network_quality_interval_mult_x10(q)) / 10
 */
uint8_t network_quality_interval_mult_x10(network_quality_t q);

/**
 * @brief Initialize the Statistics Task
 *
 * Creates the statistics task and initializes all counters to zero.
 * Task starts collecting metrics immediately.
 */
void statistics_task_init(void);

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
 * @brief Update NTRIP event counter (called by NTRIP task)
 *
 * Tracks NTRIP connection lifecycle events. CONNECTED increments
 * ntrip_reconnect_count when paired with a prior RECONNECT_BEGIN
 * (the first CONNECTED of the device's lifetime is *not* counted as
 * a reconnect). DISCONNECTED updates last_connection_state_change.
 *
 * @param event Event type — see ntrip_stats_event_t
 */
void statistics_ntrip_event(ntrip_stats_event_t event);

/**
 * @brief Update WiFi reconnect counter (called by wifi manager)
 */
void statistics_wifi_reconnect(void);

/**
 * @brief Update RTCM data received counter (called by NTRIP/GNSS tasks)
 * 
 * @param bytes Number of bytes received
 * @param messages Number of messages received
 */
void statistics_rtcm_received(uint32_t bytes, uint32_t messages);

/**
 * @brief Update GGA transmission counter (called by NTRIP task)
 *
 * @param success true if sent successfully, false if failed
 */
void statistics_gga_sent(bool success);

/**
 * @brief Update telemetry JSON receive counter (called by data output task)
 *
 * @param crc_ok true if frame CRC was valid, false if CRC failed
 */
void statistics_telemetry_received(bool crc_ok);

/**
 * @brief Update NMEA checksum error counter
 */
void statistics_nmea_checksum_error(void);

/**
 * @brief Update UART error counter
 */
void statistics_uart_error(void);

/**
 * @brief Update RTCM queue overflow counter
 */
void statistics_rtcm_queue_overflow(void);

/**
 * @brief Update GGA queue overflow counter
 */
void statistics_gga_queue_overflow(void);

/**
 * @brief Update NTRIP timeout counter
 */
void statistics_ntrip_timeout(void);

/**
 * @brief Update RTCM latency measurement
 * 
 * @param latency_ms Latency in milliseconds
 */
void statistics_rtcm_latency(uint32_t latency_ms);

/**
 * @brief Update RTCM data gap counter
 *
 * @param gap_sec Duration of the detected gap in seconds; accumulated into
 *                period.rtcm_gap_duration_sec. Pass 0 if duration unknown.
 */
void statistics_rtcm_data_gap(uint32_t gap_sec);

/**
 * @brief Increment the "config load failure" counter (called by configuration manager)
 */
void statistics_config_load_failure(void);

/**
 * @brief Increment the "task creation failure" counter (called from main on xTaskCreate failure)
 */
void statistics_task_creation_failure(void);

#ifdef __cplusplus
}
#endif

#endif // STATISTICS_TASK_H
