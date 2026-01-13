/**
 * @file buttonBootTask.cpp
 * @brief Boot button monitoring implementation
 * 
 * Implementation of BOOT button (GPIO0) monitoring with debouncing
 * logic to control LED blinking behavior.
 */

#include "buttonBootTask.h"
#include "ledIndicatorTask.h"
#include "configurationManagerTask.h"
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
    // --- Simple feature: RGB LED white when button pressed ---
    // (Commented out, see restored logic below)
    // const TickType_t pollDelay = pdMS_TO_TICKS(10);
    // bool lastButtonState = true;
    // bool buttonState = true;
    // (void)pvParameters;
    // while (1) {
    //     buttonState = gpio_get_level((gpio_num_t)BUTTON_PIN);
    //     if (lastButtonState == true && buttonState == false) {
    //         // Button pressed
    //         led_set_rgb(255, 255, 255, 0); // White, persistent until released
    //     }
    //     if (lastButtonState == false && buttonState == true) {
    //         // Button released
    //         led_set_rgb(0, 0, 0, 0); // Off
    //     }
    //     lastButtonState = buttonState;
    //     vTaskDelay(pollDelay);
    // }

    // --- Restored original logic, adapted to led_set_rgb() ---
    const TickType_t debounceDelay = pdMS_TO_TICKS(50);
    const TickType_t pollDelay = pdMS_TO_TICKS(10);
    const TickType_t fiveSeconds = pdMS_TO_TICKS(5000);
    const TickType_t tenSeconds = pdMS_TO_TICKS(10000);
    bool lastButtonState = true; // Pull-up, so high when not pressed
    bool buttonState = true;
    TickType_t pressStartTick = 0;
    bool isPressing = false;
    bool blueSet = false;
    bool greenSet = false;
    (void)pvParameters;
    ESP_LOGI(TAG, "Button Task Started - Press BOOT button to toggle LED");
    while (1) {
        buttonState = gpio_get_level((gpio_num_t)BUTTON_PIN);
        if (lastButtonState == true && buttonState == false) {
            pressStartTick = xTaskGetTickCount();
            isPressing = true;
            blueSet = false;
            greenSet = false;
            ESP_LOGI(TAG, "Button pressed");
            vTaskDelay(debounceDelay);
        }
        if (isPressing && buttonState == false) {
            TickType_t pressDuration = xTaskGetTickCount() - pressStartTick;
            if (!blueSet && pressDuration >= fiveSeconds && pressDuration < tenSeconds) {
                // Reset UI password to default
                if (config_reset_ui_password() == ESP_OK) {
                    ESP_LOGI(TAG, "UI password reset to default via button press");
                } else {
                    ESP_LOGE(TAG, "Failed to reset UI password via button press");
                }
                led_set_rgb(0, 0, 255, 0); // Blue, persistent
                blueSet = true;
            }
            if (!greenSet && pressDuration >= tenSeconds) {
                led_set_rgb(0, 255, 0, 0); // Green, persistent
                greenSet = true;
            }
        }
        if (isPressing && buttonState == true && lastButtonState == false) {
            led_set_rgb(0, 0, 0, 0); // Off
            isPressing = false;
            vTaskDelay(debounceDelay);
        }
        lastButtonState = buttonState;
        vTaskDelay(pollDelay);
    }
}

esp_err_t button_boot_task_init(void) {
    esp_err_t ret = initButton();
    if (ret != ESP_OK) {
        return ret;
    }
    BaseType_t buttonTaskResult = xTaskCreate(
        vButtonTask,
        "ButtonBootTask",
        2048,
        NULL,
        tskIDLE_PRIORITY + 1, // Normal priority
        NULL
    );
    if (buttonTaskResult != pdPASS) {
        return ESP_FAIL;
    }
    return ESP_OK;
}
