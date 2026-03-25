#pragma once

#include "esp_err.h"

/**
 * @brief Initialise and start the Sensor Emulator Task.
 *
 * Assumes UART1 has already been configured and installed by app_main()
 * with TX=GPIO4 enabled.  This task shares UART1 with
 * telemetryReceiverTask (which uses the RX=GPIO5 direction).
 *
 * The task samples all waveforms every 10 ms and feeds per-field Aggregator
 * instances.  At each CONFIG_EMUL_PERIOD_MS (default 1000 ms) publish tick
 * the accumulated min/max/avg snapshots and the latest lat/lon from the NTRIP
 * receiver are serialised into a compact JSON packet, wrapped in the binary
 * SOH/DLE-stuffed/CRC-16/CAN frame, and transmitted over UART1 TX
 * (GPIO4, 115200 8N1).
 *
 * JSON schema (see documentation/json.md for the full field reference):
 *
 *   {"seq":N,"tim":"HH:MM:SS.mmm",
 *    "vhl":{"thr":{min,max,avg},"spd":{min,max,avg},"lat":F,"lon":F},
 *    "mtr":{"pwr":{min,max,avg},"rpm":{min,max,avg},"trq":{min,max,avg}},
 *    "spc":{"vsc":{min,max,avg},"fsa":{min,max,avg},"tankP":F}}
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t sensor_emulator_task_init(void);
