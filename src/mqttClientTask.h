#ifndef MQTT_CLIENT_TASK_H
#define MQTT_CLIENT_TASK_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// MQTT message structures

// GNSS position message structure
typedef struct {
    uint32_t num;                // Message sequence number
    char daytime[32];            // ISO 8601 format: "YYYY-MM-DD HH:mm:ss.SSS"
    double lat;                  // Latitude in decimal degrees
    double lon;                  // Longitude in decimal degrees
    float alt;                   // Altitude in meters ASL
    uint8_t fix_type;            // NMEA GGA fix quality
    float speed;                 // Speed in m/s
    float dir;                   // True heading in degrees
    uint8_t sats;                // Number of satellites
    float hdop;                  // Horizontal dilution of precision
    float age;                   // Age of differential in seconds
} mqtt_gnss_message_t;

// System status message structure
typedef struct {
    char timestamp[32];          // ISO 8601 timestamp
    uint32_t uptime_sec;         // System uptime since boot
    uint32_t heap_free;          // Current free heap bytes
    uint32_t heap_min;           // Minimum free heap since boot
    bool wifi_connected;         // WiFi STA connection status
    int8_t wifi_rssi;            // WiFi signal strength (dBm)
    bool ntrip_connected;        // NTRIP connection status
    uint32_t ntrip_uptime_sec;   // NTRIP cumulative connection time
    uint32_t ntrip_reconnects;   // NTRIP reconnect count
    uint32_t rtcm_packets_total; // Total RTCM packets received
    bool mqtt_connected;         // MQTT connection status
    uint32_t mqtt_uptime_sec;    // MQTT cumulative connection time
    uint32_t mqtt_published;     // Total messages published
    uint32_t wifi_reconnects;    // WiFi reconnect count
    uint8_t current_fix;         // Current GPS fix quality
} mqtt_status_message_t;

// Statistics message structure
typedef struct {
    char timestamp[32];          // ISO 8601 timestamp
    uint32_t period_duration;    // Statistics period duration in seconds
    
    // RTCM statistics (period)
    uint32_t rtcm_bytes_received;
    uint32_t rtcm_message_rate;  // messages/sec
    uint32_t rtcm_data_gaps;
    uint32_t rtcm_avg_latency_ms; // Average RTCM latency
    uint32_t rtcm_corrupted;     // Corrupted RTCM messages
    
    // GNSS statistics (period)
    uint32_t fix_quality_duration[9]; // Seconds in each fix state
    float rtk_fixed_percent;     // Percentage in RTK fixed this period
    uint32_t time_to_rtk_fixed_sec; // Time to achieve RTK fixed
    uint32_t fix_downgrades;     // Fix quality downgrades
    uint32_t fix_upgrades;       // Fix quality upgrades
    float hdop_avg;              // Average HDOP
    float hdop_min;              // Minimum HDOP
    float hdop_max;              // Maximum HDOP
    uint8_t sats_avg;            // Average satellites
    float baseline_distance_km;  // Baseline distance to NTRIP caster
    
    // GGA transmission (period)
    uint32_t gga_sent_count;     // GGA messages sent
    uint32_t gga_failures;       // GGA send failures
    uint32_t gga_overflows;      // GGA queue overflows
    
    // System health (period)
    int8_t wifi_rssi_avg;        // Average WiFi RSSI
    int8_t wifi_rssi_min;        // Minimum WiFi RSSI
    int8_t wifi_rssi_max;        // Maximum WiFi RSSI
    float wifi_uptime_percent;   // WiFi uptime percentage
    uint32_t gnss_update_rate_hz; // GNSS update rate
    
    // Errors (period)
    uint32_t nmea_errors;        // NMEA checksum errors
    uint32_t uart_errors;        // UART errors
    uint32_t rtcm_queue_overflows; // RTCM queue overflows
    uint32_t ntrip_timeouts;     // NTRIP timeouts
} mqtt_stats_message_t;

/**
 * @brief Initialize and start the MQTT client task
 * 
 * Creates the FreeRTOS task for MQTT client operations.
 * Reads configuration from NVS and establishes connection to broker.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t mqtt_client_task_init(void);

/**
 * @brief Stop the MQTT client task
 * 
 * Gracefully disconnects from broker and deletes the task.
 */
void mqtt_client_task_stop(void);

/**
 * @brief Check if MQTT client is connected to broker
 * 
 * @return true if connected, false otherwise
 */
bool mqtt_is_connected(void);

/**
 * @brief Get total number of messages published since boot
 * 
 * @return Total message count
 */
uint32_t mqtt_get_publish_count(void);

/**
 * @brief Get cumulative MQTT connection uptime in seconds
 * 
 * @return Total seconds connected to broker since boot
 */
uint32_t mqtt_get_uptime_sec(void);

/**
 * @brief Update last activity timestamp for MQTT LED indicator
 * 
 * Called internally after each publish. Used by LED Indicator Task.
 * 
 * @param timestamp Time of last MQTT activity
 */
void mqtt_set_last_activity_time(time_t timestamp);

/**
 * @brief Get last MQTT activity timestamp
 * 
 * @return Timestamp of last publish/receive event
 */
time_t mqtt_get_last_activity_time(void);

#ifdef __cplusplus
}
#endif

#endif // MQTT_CLIENT_TASK_H
