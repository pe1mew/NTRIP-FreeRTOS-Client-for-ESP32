/**
 * @file buttonBootTask.h
 * @brief Boot button monitoring task header
 * 
 * This module provides initialization and FreeRTOS task functions for
 * monitoring the BOOT button (GPIO0) on the ESP32-S3 board.
 * 
 * @author Espressif FreeRTOS Project
 * @date 2026
 */

#ifndef ESPRESSIVE_FREERTOS_BUTTON_BOOT_TASK_H
#define ESPRESSIVE_FREERTOS_BUTTON_BOOT_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_err.h>

/**
 * @brief Initialize the button (IO0/BOOT) hardware
 * 
 * Configures GPIO0 as input with pull-up for button detection.
 * Must be called before using the button task.
 * 
 * @return ESP_OK on success, ESP error code on failure
 */
esp_err_t initButton(void);

/**
 * @brief FreeRTOS task function for monitoring button press
 * 
 * This task monitors the IO0 (BOOT) button and toggles the LED
 * blinking on/off when the button is pressed.
 * 
 * @param pvParameters Task parameters (unused, can be NULL)
 */
void vButtonTask(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // ESPRESSIVE_FREERTOS_BUTTON_BOOT_TASK_H
