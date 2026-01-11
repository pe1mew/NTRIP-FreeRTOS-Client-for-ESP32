/**
 * @file ntripClientTask.h
 * @brief NTRIP Client Task - FreeRTOS task wrapper for NTRIPClient class
 * 
 * This task manages the NTRIP client connection, receives RTCM correction data,
 * sends GGA position data, and monitors configuration changes.
 * 
 * @author ESP32-S3 NTRIP/GPS/MQTT System
 * @date 2026
 */

#ifndef NTRIP_CLIENT_TASK_H
#define NTRIP_CLIENT_TASK_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

// Queue handles for inter-task communication
extern QueueHandle_t rtcm_queue;  ///< Queue for RTCM data (NTRIP → GNSS)
extern QueueHandle_t gga_queue;   ///< Queue for GGA sentences (GNSS → NTRIP)

/**
 * @brief RTCM data structure for queue
 */
typedef struct {
    uint8_t data[512];  ///< RTCM data buffer (typical RTCM message < 500 bytes)
    size_t length;      ///< Actual data length
} rtcm_data_t;

/**
 * @brief GGA sentence structure for queue
 */
typedef struct {
    char sentence[128]; ///< GGA sentence string (NMEA max ~82 chars)
} gga_data_t;

/**
 * @brief Initialize NTRIP client task and queues
 * 
 * Creates the RTCM and GGA queues, then starts the NTRIP client task.
 * The task monitors CONFIG_NTRIP_CHANGED_BIT for configuration updates
 * and manages the connection lifecycle.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ntrip_client_task_init(void);

/**
 * @brief Get NTRIP client connection status
 * 
 * @return true if connected to NTRIP caster, false otherwise
 */
bool ntrip_client_is_connected(void);

/**
 * @brief Get NTRIP client connection status - Compatibility wrapper
 * 
 * Same as ntrip_client_is_connected, provided for naming consistency
 * 
 * @return true if connected to NTRIP caster, false otherwise
 */
static inline bool ntrip_is_connected(void) {
    return ntrip_client_is_connected();
}

/**
 * @brief Get NTRIP client uptime in seconds
 * 
 * Returns the cumulative time the NTRIP client has been connected
 * 
 * @return Total connection time in seconds
 */
uint32_t ntrip_get_uptime_sec(void);

/**
 * @brief Stop NTRIP client task and cleanup resources
 * 
 * Disconnects from NTRIP caster, deletes queues, and stops the task.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ntrip_client_task_stop(void);

#ifdef __cplusplus
}
#endif

#endif // NTRIP_CLIENT_TASK_H
