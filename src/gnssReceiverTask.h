#ifndef GNSS_RECEIVER_TASK_H
#define GNSS_RECEIVER_TASK_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

// Event bits for GNSS data updates
#define GNSS_DATA_UPDATED_BIT   (1 << 0)
#define GNSS_GGA_UPDATED_BIT    (1 << 1)

// Global event group for GNSS data notifications
extern EventGroupHandle_t gnss_event_group;

// GNSS data structure containing latest parsed NMEA data
typedef struct {
    // Raw NMEA sentences (for NTRIP GGA forwarding)
    char gga[128];      // Latest GGA sentence
    char rmc[128];      // Latest RMC sentence
    char vtg[128];      // Latest VTG sentence
    
    // Parsed position data
    double latitude;    // Decimal degrees (signed)
    double longitude;   // Decimal degrees (signed)
    float altitude;     // Meters
    float heading;      // Degrees (0-359.99)
    float speed;        // km/h
    
    // Time data
    uint8_t day;        // Day (01-31)
    uint8_t month;      // Month (01-12)
    uint8_t year;       // Year (00-99, 2000+)
    uint8_t hour;       // Hour (00-23)
    uint8_t minute;     // Minute (00-59)
    uint8_t second;     // Second (00-59)
    uint16_t millisecond; // Millisecond (000-999)
    
    // Quality indicators
    uint8_t fix_quality; // 0=no fix, 1=GPS, 2=DGPS, 4=RTK fixed, 5=RTK float
    uint8_t satellites;  // Number of satellites
    float hdop;          // Horizontal dilution of precision
    
    // Status
    time_t timestamp;   // Last update time
    bool valid;         // Data validity flag
} gnss_data_t;

// GNSS configuration structure
typedef struct {
    uint16_t gga_interval_sec;  // GGA send interval to NTRIP (default: 120)
} gnss_config_t;

// Initialize and start the GNSS Receiver Task
void gnss_receiver_task_init(void);

// Get a copy of the latest GNSS data (thread-safe)
void gnss_get_data(gnss_data_t *data);

// Check if GNSS has valid fix
bool gnss_has_valid_fix(void);

// Stop the GNSS Receiver Task
void gnss_receiver_task_stop(void);

#endif // GNSS_RECEIVER_TASK_H
