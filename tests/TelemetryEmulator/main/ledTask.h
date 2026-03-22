#pragma once

#include "esp_err.h"

/**
 * @brief Initialise the WS2812 RGB LED and start the LED task.
 *
 * Must be called from app_main() before any task that calls led_blink_*().
 * Configures the RMT TX channel (GPIO38, 40 MHz) and a WS2812 bytes encoder
 * using the same settings as ledIndicatorTask in the NTRIP Client project.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t led_task_init(void);

/**
 * @brief Flash the blue channel for LED_BLINK_MS milliseconds.
 * Called by sensorEmulatorTask after each frame transmission.
 */
void led_blink_blue(void);

/**
 * @brief Flash the green channel for LED_BLINK_MS milliseconds.
 * Called by telemetryReceiverTask on a frame with a valid CRC.
 */
void led_blink_green(void);

/**
 * @brief Flash the red channel for LED_BLINK_MS milliseconds.
 * Called by telemetryReceiverTask on a frame with a CRC error.
 */
void led_blink_red(void);
