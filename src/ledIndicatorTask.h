#ifndef LED_INDICATOR_TASK_H
#define LED_INDICATOR_TASK_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// GGA Fix Quality Values (from NMEA GGA sentence field 6)
#define GPS_FIX_NONE       0  // No fix
#define GPS_FIX_GPS        1  // GPS fix (SPS)
#define GPS_FIX_DGPS       2  // DGPS fix
#define GPS_FIX_PPS        3  // PPS fix
#define GPS_FIX_RTK_FIXED  4  // RTK Fixed
#define GPS_FIX_RTK_FLOAT  5  // RTK Float
#define GPS_FIX_ESTIMATED  6  // Estimated (dead reckoning)
#define GPS_FIX_MANUAL     7  // Manual input mode
#define GPS_FIX_SIMULATION 8  // Simulation mode

// Initialize and start the LED Indicator Task
void led_indicator_task_init(void);

// Update activity timestamps (called by respective tasks)
void led_update_ntrip_activity(void);
void led_update_mqtt_activity(void);

// Stop the LED Indicator Task
void led_indicator_task_stop(void);

#endif // LED_INDICATOR_TASK_H
