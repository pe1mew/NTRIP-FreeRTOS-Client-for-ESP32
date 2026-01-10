#ifndef GNSS_RECEIVER_TASK_H
#define GNSS_RECEIVER_TASK_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// GNSS data structure containing latest parsed NMEA sentences
typedef struct {
    char gga[128];      // Latest GGA sentence
    char rmc[128];      // Latest RMC sentence
    char vtg[128];      // Latest VTG sentence
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
