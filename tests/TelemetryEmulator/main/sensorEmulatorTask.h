#pragma once

#include "esp_err.h"

/**
 * @brief Initialise and start the Sensor Emulator Task.
 *
 * Assumes UART1 has already been configured and installed by app_main()
 * with TX=GPIO4 enabled.  This task shares UART1 with
 * telemetryReceiverTask (which uses the RX=GPIO5 direction).
 *
 * The task publishes a stringified JSON telemetry packet every 1 second
 * over UART1 TX (GPIO4, 115200 8N1).
 *
 * Each field in the JSON is driven by a deterministic periodic waveform
 * (sine, cosine, triangle, square, trapezoid, or rectified sine) with
 * amplitude scaled to the min/max specified by the MQTT interface.
 *
 * JSON schema (mirrors the MQTT payload defined in sensorData.md):
 *
 *   {"seq":N,"tim":"HH:MM:SS.mmm",
 *    "vhl":{"accX":...,"accY":...,"accZ":...,"thr":...},
 *    "mtr":{"pwr":...,"rpm":...,"trq":...},
 *    "spc":{"vsc":...,"tankP":...,"fan":...,"h2P1":...,"h2P2":...}}
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t sensor_emulator_task_init(void);
