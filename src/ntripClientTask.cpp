/**
 * @file ntripClientTask.cpp
 * @brief NTRIP Client Task implementation
 * 
 * This task wraps the NTRIPClient class and manages:
 * - Connection to NTRIP caster based on configuration
 * - Receiving RTCM correction data and forwarding to GNSS
 * - Receiving GGA position data and sending to NTRIP caster
 * - Reconnection on disconnect with configurable delay
 * - Configuration change monitoring via event groups
 */

#include "ntripClientTask.h"
#include "NTRIPclient/NTRIPClient.h"
#include "configurationManagerTask.h"
#include "ledIndicatorTask.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <freertos/event_groups.h>
#include <cstring>

static const char* TAG = "NTRIPTask";

// Queue handles (global, accessible from other tasks)
QueueHandle_t rtcm_queue = NULL;
QueueHandle_t gga_queue = NULL;

// Task handle
static TaskHandle_t ntrip_task_handle = NULL;

// Connection state
static bool ntrip_connected = false;

// Queue configuration
#define RTCM_QUEUE_LENGTH   10  // Can buffer 10 RTCM messages
#define GGA_QUEUE_LENGTH    5   // Can buffer 5 GGA sentences

// Task configuration
#define NTRIP_TASK_STACK_SIZE   8192
#define NTRIP_TASK_PRIORITY     3

/**
 * @brief NTRIP Client Task main function
 */
static void ntrip_client_task(void* pvParameters) {
    NTRIPClient* client = new NTRIPClient();
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to allocate NTRIPClient");
        vTaskDelete(NULL);
        return;
    }
    
    ntrip_config_t ntrip_config;
    int64_t last_gga_time = 0;
    int64_t last_connect_attempt = 0;
    bool reconnect_needed = false;
    
    ESP_LOGI(TAG, "NTRIP Client Task started");
    
    // Get initial configuration
    if (config_get_ntrip(&ntrip_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get initial NTRIP configuration");
        delete client;
        vTaskDelete(NULL);
        return;
    }
    
    while (1) {
        // Check for configuration changes
        EventBits_t bits = config_wait_for_event(CONFIG_NTRIP_CHANGED_BIT, 0);
        if (bits & CONFIG_NTRIP_CHANGED_BIT) {
            ESP_LOGI(TAG, "NTRIP configuration changed");
            
            // Get new configuration first
            if (config_get_ntrip(&ntrip_config) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to get updated NTRIP configuration");
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
            
            // Disconnect if currently connected (either disabled or config changed)
            if (ntrip_connected) {
                if (ntrip_config.enabled) {
                    ESP_LOGI(TAG, "Disconnecting to apply new NTRIP configuration");
                } else {
                    ESP_LOGI(TAG, "NTRIP disabled, disconnecting");
                }
                client->disconnect();
                ntrip_connected = false;
            }
            
            reconnect_needed = ntrip_config.enabled;
        }
        
        // Handle connection state
        if (ntrip_config.enabled && !ntrip_connected) {
            int64_t now = esp_timer_get_time();
            int64_t time_since_last_attempt = (now - last_connect_attempt) / 1000000;
            
            // Check if enough time has passed since last connection attempt
            if (reconnect_needed || time_since_last_attempt >= ntrip_config.reconnect_delay_sec) {
                last_connect_attempt = now;
                reconnect_needed = false;
                
                ESP_LOGI(TAG, "Connecting to NTRIP caster: %s:%d/%s", 
                         ntrip_config.host, ntrip_config.port, ntrip_config.mountpoint);
                
                // Initialize client
                if (!client->init()) {
                    ESP_LOGE(TAG, "Failed to initialize NTRIP client");
                    vTaskDelay(pdMS_TO_TICKS(ntrip_config.reconnect_delay_sec * 1000));
                    continue;
                }
                
                // Convert port to int for NTRIPClient API (expects int&)
                int port = ntrip_config.port;
                
                // Connect to NTRIP caster
                bool connect_success = false;
                if (strlen(ntrip_config.user) > 0) {
                    connect_success = client->reqRaw(ntrip_config.host, port, 
                                                     ntrip_config.mountpoint, 
                                                     ntrip_config.user, ntrip_config.password);
                } else {
                    connect_success = client->reqRaw(ntrip_config.host, port, 
                                                     ntrip_config.mountpoint);
                }
                
                if (connect_success && client->isConnected()) {
                    ntrip_connected = true;
                    last_gga_time = 0; // Reset GGA timer to send immediately
                    ESP_LOGI(TAG, "Successfully connected to NTRIP caster");
                } else {
                    ESP_LOGW(TAG, "Failed to connect to NTRIP caster, will retry in %d seconds", 
                             ntrip_config.reconnect_delay_sec);
                    client->disconnect();
                    ntrip_connected = false;
                }
            }
        } else if (!ntrip_config.enabled && ntrip_connected) {
            // NTRIP disabled, disconnect
            ESP_LOGI(TAG, "NTRIP disabled, disconnecting");
            client->disconnect();
            ntrip_connected = false;
        }
        
        // Handle connected state operations
        if (ntrip_connected && client->isConnected()) {
            // Check for incoming RTCM data
            if (client->available() > 0) {
                rtcm_data_t rtcm_msg;
                int bytes_read = client->readData(rtcm_msg.data, sizeof(rtcm_msg.data));
                
                if (bytes_read > 0) {
                    rtcm_msg.length = bytes_read;
                    
                    // Notify LED task of RTCM data activity
                    led_update_ntrip_activity();
                    
                    // Send to GNSS via queue (ring buffer behavior - drop oldest if full)
                    if (xQueueSend(rtcm_queue, &rtcm_msg, 0) != pdTRUE) {
                        // Queue full - remove oldest item and add new one (ring buffer)
                        rtcm_data_t dummy;
                        if (xQueueReceive(rtcm_queue, &dummy, 0) == pdTRUE) {
                            // Successfully removed old item, try adding new one again
                            if (xQueueSend(rtcm_queue, &rtcm_msg, 0) != pdTRUE) {
                                ESP_LOGW(TAG, "Failed to add RTCM data after removing old item");
                            } else {
                                ESP_LOGD(TAG, "RTCM queue full, dropped oldest data for new (%d bytes)", bytes_read);
                            }
                        } else {
                            ESP_LOGW(TAG, "RTCM queue full and couldn't remove old data");
                        }
                    } else {
                        ESP_LOGD(TAG, "Received %d bytes RTCM data", bytes_read);
                    }
                }
            }
            
            // Check for GGA sentences to send
            gga_data_t gga_msg;
            if (xQueueReceive(gga_queue, &gga_msg, 0) == pdTRUE) {
                client->sendGGA(gga_msg.sentence);
                last_gga_time = esp_timer_get_time();
                ESP_LOGD(TAG, "Sent GGA: %s", gga_msg.sentence);
            } else {
                // Send GGA periodically even if not received from GNSS (keep connection alive)
                int64_t now = esp_timer_get_time();
                int64_t time_since_last_gga = (now - last_gga_time) / 1000000;
                
                if (time_since_last_gga >= ntrip_config.gga_interval_sec) {
                    // Send a dummy GGA to keep connection alive
                    // In real implementation, GNSS task should be sending actual GGA
                    last_gga_time = now;
                    ESP_LOGD(TAG, "GGA interval elapsed (%d sec), waiting for GNSS data", 
                             ntrip_config.gga_interval_sec);
                }
            }
            
            // Verify connection is still active
            if (!client->isConnected()) {
                ESP_LOGW(TAG, "Connection lost, will attempt reconnect");
                ntrip_connected = false;
                reconnect_needed = true;
            }
        }
        
        // Task delay to prevent tight loop
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Cleanup (shouldn't reach here unless task is deleted)
    delete client;
    vTaskDelete(NULL);
}

esp_err_t ntrip_client_task_init(void) {
    ESP_LOGI(TAG, "Initializing NTRIP Client Task");
    
    // Create RTCM queue
    rtcm_queue = xQueueCreate(RTCM_QUEUE_LENGTH, sizeof(rtcm_data_t));
    if (rtcm_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create RTCM queue");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "RTCM queue created (length: %d)", RTCM_QUEUE_LENGTH);
    
    // Create GGA queue
    gga_queue = xQueueCreate(GGA_QUEUE_LENGTH, sizeof(gga_data_t));
    if (gga_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create GGA queue");
        vQueueDelete(rtcm_queue);
        rtcm_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "GGA queue created (length: %d)", GGA_QUEUE_LENGTH);
    
    // Create NTRIP client task
    BaseType_t result = xTaskCreate(
        ntrip_client_task,
        "NTRIP_Client",
        NTRIP_TASK_STACK_SIZE,
        NULL,
        NTRIP_TASK_PRIORITY,
        &ntrip_task_handle
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create NTRIP client task");
        vQueueDelete(rtcm_queue);
        vQueueDelete(gga_queue);
        rtcm_queue = NULL;
        gga_queue = NULL;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "NTRIP Client Task initialized successfully");
    return ESP_OK;
}

bool ntrip_client_is_connected(void) {
    return ntrip_connected;
}

esp_err_t ntrip_client_task_stop(void) {
    ESP_LOGI(TAG, "Stopping NTRIP Client Task");
    
    // Delete task
    if (ntrip_task_handle != NULL) {
        vTaskDelete(ntrip_task_handle);
        ntrip_task_handle = NULL;
    }
    
    // Delete queues
    if (rtcm_queue != NULL) {
        vQueueDelete(rtcm_queue);
        rtcm_queue = NULL;
    }
    
    if (gga_queue != NULL) {
        vQueueDelete(gga_queue);
        gga_queue = NULL;
    }
    
    ntrip_connected = false;
    
    ESP_LOGI(TAG, "NTRIP Client Task stopped");
    return ESP_OK;
}
