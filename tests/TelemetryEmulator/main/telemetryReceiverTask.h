#pragma once

#include "esp_err.h"

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
