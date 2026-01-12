/**
 * @file ledIndicatorTask.h
 * @brief LED indicator task for system status visualization on ESP32.
 *
 * Provides API for initializing, updating, and stopping the LED indicator task, which manages discrete and RGB LEDs
 * to reflect WiFi, NTRIP, MQTT, and GNSS/RTK status.
 */

#ifndef LED_INDICATOR_TASK_H
#define LED_INDICATOR_TASK_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

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

/**
 * @brief Initialize and start the LED Indicator Task.
 *
 * This task manages all status LEDs and should be called once during system startup.
 */
void led_indicator_task_init(void);

/**
 * @brief Update NTRIP activity timestamp.
 *
 * Call this from the NTRIP client task when new data is received to indicate activity.
 */
void led_update_ntrip_activity(void);

/**
 * @brief Update MQTT activity timestamp.
 *
 * Call this from the MQTT client task when new data is received to indicate activity.
 */
void led_update_mqtt_activity(void);

/**
 * @brief Stop the LED Indicator Task and turn off all LEDs.
 */
void led_indicator_task_stop(void);

#endif // LED_INDICATOR_TASK_H
