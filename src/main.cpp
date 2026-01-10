#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_system.h>
#include "helloWorld/rgbLedTask.h"
#include "helloWorld/buttonBootTask.h"

static const char *TAG = "MAIN";

extern "C" void app_main(void) {
    // Initialize logging
    ESP_LOGI(TAG, "\n\n=================================");
    ESP_LOGI(TAG, "Lolin S3 FreeRTOS RGB LED Demo");
    ESP_LOGI(TAG, "=================================\n");

    // Initialize the RGB LED hardware
    esp_err_t ret = initRGBLed();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize RGB LED: %s", esp_err_to_name(ret));
        return;
    }

    // Initialize the button hardware
    ret = initButton();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize button: %s", esp_err_to_name(ret));
        return;
    }

    // Create FreeRTOS task for blinking RGB LED
    xTaskCreate(
        vBlinkRGBTask,           // Task function
        "RGB_Blink",             // Task name
        2048,                    // Stack size (bytes)
        NULL,                    // Task parameters
        1,                       // Task priority
        NULL                     // Task handle
    );

    // Create FreeRTOS task for button monitoring
    xTaskCreate(
        vButtonTask,             // Task function
        "Button_Monitor",        // Task name
        2048,                    // Stack size (bytes)
        NULL,                    // Task parameters
        2,                       // Task priority (higher than LED task)
        NULL                     // Task handle
    );

    ESP_LOGI(TAG, "FreeRTOS tasks created successfully");
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
}
