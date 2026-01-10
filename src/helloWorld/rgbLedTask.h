/**
 * @file rgbLedTask.h
 * @brief RGB LED control task header for WS2812B LED strip
 * 
 * This module provides initialization and FreeRTOS task functions for
 * controlling a WS2812B RGB LED using the ESP32 RMT peripheral.
 * 
 * @author Espressif FreeRTOS Project
 * @date 2026
 */

#ifndef ESPRESSIVE_FREERTOS_RGB_LED_TASK_H
#define ESPRESSIVE_FREERTOS_RGB_LED_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_err.h>
#include <stdbool.h>

/**
 * @brief Initialize the RGB LED hardware
 * 
 * Configures the RMT peripheral and creates the LED strip encoder
 * for WS2812B protocol communication. Must be called before using
 * the LED task.
 * 
 * @return ESP_OK on success, ESP error code on failure
 */
esp_err_t initRGBLed(void);

/**
 * @brief FreeRTOS task function for cycling through RGB LED colors
 * 
 * This task cycles through predefined colors with blinking pattern.
 * Each color is displayed for 500ms with 500ms off time between changes.
 * Can be toggled on/off via the button task.
 * 
 * @param pvParameters Task parameters (unused, can be NULL)
 */
void vBlinkRGBTask(void *pvParameters);

/**
 * @brief Toggle the LED enabled state
 * 
 * Toggles between enabled and disabled state for LED blinking.
 */
void toggleLedEnabled(void);

/**
 * @brief Check if LED is enabled
 * 
 * @return true if LED blinking is enabled, false otherwise
 */
bool isLedEnabled(void);

#ifdef __cplusplus
}
#endif

#endif // ESPRESSIVE_FREERTOS_RGB_LED_TASK_H
