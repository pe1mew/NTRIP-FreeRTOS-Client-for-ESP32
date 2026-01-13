/**
 * @file dataOutputTask.h
 * @brief Data Output Task - Telemetry data transmission via UART1
 * 
 * This task formats and transmits GPS position, time, and navigation data
 * to an external telemetry unit using a binary framing protocol with CRC-16.
 * 
 * @author ESP32-S3 NTRIP/GPS/MQTT System
 * @date 2026
 */

#ifndef DATA_OUTPUT_TASK_H
#define DATA_OUTPUT_TASK_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @def DATA_OUTPUT_INTERVAL_MS
 * @brief Default output interval in milliseconds (10 Hz).
 */
#define DATA_OUTPUT_INTERVAL_MS     100
/**
 * @def DATA_OUTPUT_TASK_STACK_SIZE
 * @brief Stack size for the data output task.
 */
#define DATA_OUTPUT_TASK_STACK_SIZE 4096
/**
 * @def DATA_OUTPUT_TASK_PRIORITY
 * @brief FreeRTOS priority for the data output task.
 */
#define DATA_OUTPUT_TASK_PRIORITY   2

/**
 * @def FRAME_SOH
 * @brief Start of header byte for binary framing (Control-A).
 */
#define FRAME_SOH   0x01
/**
 * @def FRAME_CAN
 * @brief Cancel byte for binary framing (Control-X).
 */
#define FRAME_CAN   0x18
/**
 * @def FRAME_DLE
 * @brief Data link escape byte for binary framing.
 */
#define FRAME_DLE   0x10

/**
 * @brief Data output configuration structure
 */
/**
 * @brief Data output configuration structure.
 */
typedef struct {
    uint32_t interval_ms;   /**< Output interval in milliseconds */
    bool enabled;           /**< Enable/disable data output */
} data_output_config_t;

/**
 * @brief Position data structure for telemetry output
 */
/**
 * @brief Position data structure for telemetry output.
 */
typedef struct {
    uint8_t day;            /**< Day (01-31) */
    uint8_t month;          /**< Month (01-12) */
    uint8_t year;           /**< Year (00-99, 2000+) */
    uint8_t hour;           /**< Hour (00-23) */
    uint8_t minute;         /**< Minute (00-59) */
    uint8_t second;         /**< Second (00-59) */
    uint16_t millisecond;   /**< Millisecond (000-999) */
    double latitude;        /**< Latitude in decimal degrees (signed) */
    double longitude;       /**< Longitude in decimal degrees (signed) */
    float altitude;         /**< Altitude in meters */
    float heading;          /**< True heading in degrees (0-359.99) */
    float speed;            /**< Ground speed in km/h */
    bool valid;             /**< Data validity flag */
    uint8_t fix_quality;    /**< GNSS fix quality (0=no fix, 1=GPS, 2=DGPS, 4=RTK fixed, 5=RTK float) */
} position_data_t;

/**
 * @brief Initialize and start the Data Output Task
 * 
 * Creates UART1 interface and starts the telemetry transmission task.
 * The task monitors configuration changes and can be enabled/disabled.
 * 
 * @return ESP_OK on success, error code otherwise
 */
/**
 * @brief Initialize and start the Data Output Task.
 *
 * Creates UART1 interface and starts the telemetry transmission task.
 * The task monitors configuration changes and can be enabled/disabled.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t data_output_task_init(void);

/**
 * @brief Stop the Data Output Task and cleanup resources
 * 
 * Stops the task, closes UART, and releases resources.
 * 
 * @return ESP_OK on success
 */
/**
 * @brief Stop the Data Output Task and cleanup resources.
 *
 * Stops the task, closes UART, and releases resources.
 *
 * @return ESP_OK on success
 */
esp_err_t data_output_task_stop(void);

#ifdef __cplusplus
}
#endif

#endif // DATA_OUTPUT_TASK_H
