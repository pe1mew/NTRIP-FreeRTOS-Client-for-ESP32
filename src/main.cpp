#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include "configurationManagerTask.h"
#include "wifiManager.h"
#include "httpServer.h"
#include "ntripClientTask.h"
#include "gnssReceiverTask.h"
#include "dataOutputTask.h"
#include "statisticsTask.h"
#include "mqttClientTask.h"

#include "ledIndicatorTask.h"
#include "buttonBootTask.h"

static const char *TAG = "MAIN";

extern "C" void app_main(void) {
    esp_err_t ret;
    
    // Initialize logging
    ESP_LOGI(TAG, "\n\n===========================================");
    ESP_LOGI(TAG, "ESP32-S3 NTRIP/GPS/MQTT System Starting...");
    ESP_LOGI(TAG, "===========================================\n");
    
    // ========================================
    // Step 1: Initialize NVS Flash
    // ========================================
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs to be erased");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "✓ NVS Flash initialized");
    
    // ========================================
    // Step 2: Initialize Configuration Manager
    // ========================================
    ret = config_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Configuration Manager: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "✓ Configuration Manager initialized");
    
    // ========================================
    // Step 3: Initialize WiFi Manager (AP+STA)
    // ========================================
    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi Manager: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "✓ WiFi Manager initialized (AP mode: 192.168.4.1)");
    
    // ========================================
    // Step 4: Initialize HTTP Server
    // ========================================
    ret = http_server_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize HTTP Server: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "✓ HTTP Server initialized (port 80)");
    
    // ========================================
    // Step 5: Initialize NTRIP Client Task
    // ========================================
    ret = ntrip_client_task_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NTRIP Client Task: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "✓ NTRIP Client Task initialized");
    
    // ========================================
    // Step 6: Initialize GNSS Receiver Task
    // ========================================
    gnss_receiver_task_init();
    ESP_LOGI(TAG, "✓ GNSS Receiver Task initialized");
    
    // ========================================
    // Step 7: Initialize Data Output Task
    // ========================================
    ret = data_output_task_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Data Output Task: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "✓ Data Output Task initialized");
    
    // ========================================
    // Step 8: Initialize LED Indicator Task
    // ========================================
    led_indicator_task_init();
    ESP_LOGI(TAG, "✓ LED Indicator Task initialized");
    
    // ========================================
    // Step 9: Initialize Statistics Task
    // ========================================
    statistics_task_init();
    ESP_LOGI(TAG, "✓ Statistics Task initialized");
    
    // ========================================
    // Step 10: Initialize MQTT Client Task
    // ========================================
    ret = mqtt_client_task_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MQTT Client Task initialization failed or disabled: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "✓ MQTT Client Task initialized");
    }
    
    // ========================================
    // Step 11: Initialize Button Boot Task
    // ========================================
    ret = button_boot_task_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Button Boot Task: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "\u2713 Button Boot Task initialized");
    }
    
    // ========================================
    // System Ready
    // ========================================
    ESP_LOGI(TAG, "\n===========================================");
    ESP_LOGI(TAG, "System Initialization Complete!");
    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "Configuration Interface: http://192.168.4.1");
    ESP_LOGI(TAG, "Check WiFi Manager logs above for AP SSID");
    ESP_LOGI(TAG, "Free heap: %lu bytes\n", esp_get_free_heap_size());
}
