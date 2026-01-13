/**
 * @file ledIndicatorTask.h
 * @brief LED indicator task for ESP32.
 *
 * This header provides the API for initializing, updating, and stopping the LED indicator task, which manages discrete and RGB LEDs.
 * The task allows other system components to control the RGB LED via a FreeRTOS queue, and provides functions to set the LED color directly or for a specified duration.
 */

#ifndef LED_INDICATOR_TASK_H
#define LED_INDICATOR_TASK_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * @name GGA Fix Quality Values
 * @brief Fix quality values from NMEA GGA sentence field 6.
 */
#define GPS_FIX_NONE       0  /**< No fix */
#define GPS_FIX_GPS        1  /**< GPS fix (SPS) */
#define GPS_FIX_DGPS       2  /**< DGPS fix */
#define GPS_FIX_PPS        3  /**< PPS fix */
#define GPS_FIX_RTK_FIXED  4  /**< RTK Fixed */
#define GPS_FIX_RTK_FLOAT  5  /**< RTK Float */
#define GPS_FIX_ESTIMATED  6  /**< Estimated (dead reckoning) */
#define GPS_FIX_MANUAL     7  /**< Manual input mode */
#define GPS_FIX_SIMULATION 8  /**< Simulation mode */
/**@}*/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start the LED Indicator Task.
 *
 * Creates the FreeRTOS task and queue for LED control. Should be called once during system startup.
 */
void led_indicator_task_init(void);

/**
 * @brief Update NTRIP activity timestamp.
 *
 * Call from the NTRIP client task when new data is received to indicate activity.
 */
void led_update_ntrip_activity(void);

/**
 * @brief Update MQTT activity timestamp.
 *
 * Call from the MQTT client task when new data is received to indicate activity.
 */
void led_update_mqtt_activity(void);

/**
 * @brief Stop the LED Indicator Task and turn off all LEDs.
 *
 * Deletes the LED indicator task and turns off all discrete and RGB LEDs.
 */
void led_indicator_task_stop(void);

/**
 * @brief Initialize the RGB LED hardware (RMT driver).
 *
 * Should be called before using set_led_color or led_set_rgb if not using led_indicator_task_init.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t initRGBLed(void);

/**
 * @brief Set the RGB LED to a specific color immediately.
 *
 * @param red   Red component (0-255)
 * @param green Green component (0-255)
 * @param blue  Blue component (0-255)
 */
void set_led_color(uint8_t red, uint8_t green, uint8_t blue);

/**
 * @brief Queue a command to set the RGB LED color for a specified duration.
 *
 * @param r              Red component (0-255)
 * @param g              Green component (0-255)
 * @param b              Blue component (0-255)
 * @param duration_ticks Duration in FreeRTOS ticks (0 = persistent)
 */
void led_set_rgb(uint8_t r, uint8_t g, uint8_t b, TickType_t duration_ticks);

#ifdef __cplusplus
}
#endif

#endif // LED_INDICATOR_TASK_H
