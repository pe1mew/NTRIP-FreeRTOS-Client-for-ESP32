/**
 * @file buttonBootTask.cpp
 * @brief Boot button monitoring implementation
 * 
 * Implementation of BOOT button (GPIO0) monitoring with debouncing
 * logic to control LED blinking behavior.
 */

#include "buttonBootTask.h"
#include "rgbLedTask.h"
#include <esp_log.h>
#include <driver/gpio.h>

static const char *TAG = "BUTTON";

/** @defgroup Hardware Configuration
 * @{
 */
#define BUTTON_PIN      0     /**< GPIO pin for BOOT button (IO0) */
/** @} */

/**
 * @brief Initialize button GPIO
 * 
 * Configures GPIO0 (BOOT button) as input with pull-up resistor
 * for button press detection.
 * 
 * @return ESP_OK on success, ESP error code on failure
 */
esp_err_t initButton(void) {
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << BUTTON_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Button initialized on GPIO %d", BUTTON_PIN);
    }
    return ret;
}

/**
 * @brief FreeRTOS task for button monitoring and LED toggle control
 * 
 * Monitors the BOOT button (GPIO0) and toggles LED blinking on/off
 * when pressed. Includes debouncing logic.
 * 
 * @param pvParameters Task parameters (unused)
 */
void vButtonTask(void *pvParameters) {
    const TickType_t debounceDelay = pdMS_TO_TICKS(50);
    bool lastButtonState = true; // Pull-up, so high when not pressed
    bool buttonState = true;
    
    // Suppress unused parameter warning
    (void)pvParameters;
    
    ESP_LOGI(TAG, "Button Task Started - Press BOOT button to toggle LED");
    
    while (1) {
        // Read current button state (0 = pressed, 1 = not pressed)
        buttonState = gpio_get_level((gpio_num_t)BUTTON_PIN);
        
        // Detect button press (transition from high to low)
        if (lastButtonState == true && buttonState == false) {
            // Button pressed - toggle LED state
            toggleLedEnabled();
            ESP_LOGI(TAG, "Button pressed - LED blinking %s", isLedEnabled() ? "ENABLED" : "DISABLED");
            
            // Wait for button release with debouncing
            vTaskDelay(debounceDelay);
            while (gpio_get_level((gpio_num_t)BUTTON_PIN) == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            // Additional debounce delay after release
            vTaskDelay(debounceDelay);
        }
        
        lastButtonState = buttonState;
        vTaskDelay(pdMS_TO_TICKS(10)); // Poll every 10ms
    }
}
