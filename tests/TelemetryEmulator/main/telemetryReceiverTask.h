#pragma once

#include "esp_err.h"

/**
 * @brief Data snapshot from the last successfully received NTRIP frame.
 *
 * Payload format: `YYYY-MM-DD HH:mm:ss.sss,LAT,LON,ALT,HEADING,SPEED`
 * All fields are zero/empty-initialised until the first valid CRC-OK frame arrives.
 */
struct NtripLatLon {
    double lat;      ///< Latitude in decimal degrees (positive = North).
    double lon;      ///< Longitude in decimal degrees (positive = East).
    char   tim[13];  ///< Wall-clock time from last NTRIP frame, format "HH:MM:SS.mmm". Empty string until first valid frame.
};

/**
 * @brief Return a thread-safe copy of the most recently decoded lat/lon.
 *
 * Safe to call from any task. Returns {0.0, 0.0} if no valid NTRIP frame has
 * been received yet, or if called before telemetry_receiver_task_init().
 *
 * @return NtripLatLon snapshot.
 */
NtripLatLon telemetry_receiver_get_latlon(void);

/**
 * @brief Initialise and start the Telemetry Receiver Task.
 *
 * Assumes UART1 has already been configured and installed by app_main().
 * The task decodes binary-framed position telemetry from the NTRIP Client,
 * validates the CRC-16/CCITT-FALSE checksum, and reports results via ESP_LOG.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t telemetry_receiver_task_init(void);
