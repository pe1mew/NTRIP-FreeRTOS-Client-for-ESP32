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
#include "wifiManager.h"
#include "statisticsTask.h"
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
static time_t ntrip_connection_start = 0;
static uint32_t ntrip_uptime_accumulated = 0;

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
    int64_t last_config_poll = 0; // microseconds
    
    ESP_LOGI(TAG, "NTRIP Client Task started");
    
    // Get initial configuration
    if (config_get_ntrip(&ntrip_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get initial NTRIP configuration");
        delete client;
        vTaskDelete(NULL);
        return;
    }
    
    // Get configuration event group handle once
    EventGroupHandle_t config_events = config_get_event_group();

    while (1) {
        // Poll for configuration changes and clear handled bits (matches MQTT behavior)
        EventBits_t bits = xEventGroupGetBits(config_events);
        if (bits & CONFIG_NTRIP_CHANGED_BIT) {
            // Clear the specific bit we are handling
            xEventGroupClearBits(config_events, CONFIG_NTRIP_CHANGED_BIT);
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
        } else if (bits & CONFIG_ALL_CHANGED_BIT) {
            // Clear the global change bit as we will refresh based on it
            xEventGroupClearBits(config_events, CONFIG_ALL_CHANGED_BIT);
            // Global config changed; refresh NTRIP config and apply changes similarly
            ESP_LOGI(TAG, "Global configuration changed; refreshing NTRIP settings");
            if (config_get_ntrip(&ntrip_config) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to get updated NTRIP configuration");
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
            if (ntrip_connected) {
                if (ntrip_config.enabled) {
                    ESP_LOGI(TAG, "Disconnecting to apply new NTRIP configuration (global change)");
                } else {
                    ESP_LOGI(TAG, "NTRIP disabled (global change), disconnecting");
                }
                client->disconnect();
                ntrip_connected = false;
            }
            reconnect_needed = ntrip_config.enabled;
        }

        // Periodic config poll to avoid missed events (once per second)
        int64_t now_us = esp_timer_get_time();
        if ((now_us - last_config_poll) >= 1000000) {
            last_config_poll = now_us;
            ntrip_config_t polled_config;
            if (config_get_ntrip(&polled_config) == ESP_OK) {
                if (polled_config.enabled != ntrip_config.enabled) {
                    ESP_LOGI(TAG, "NTRIP enabled changed via poll: %s -> %s",
                             ntrip_config.enabled ? "true" : "false",
                             polled_config.enabled ? "true" : "false");
                    // Update local copy
                    ntrip_config = polled_config;
                    if (!ntrip_config.enabled && ntrip_connected) {
                        ESP_LOGI(TAG, "Polling detected disable, disconnecting NTRIP");
                        client->disconnect();
                        ntrip_connected = false;
                        reconnect_needed = false;
                    } else if (ntrip_config.enabled && !ntrip_connected) {
                        reconnect_needed = true;
                    }
                } else {
                    // Keep other fields fresh in case they changed without events
                    ntrip_config = polled_config;
                }
            }
        }
        
        // Handle connection state
        if (ntrip_config.enabled && !ntrip_connected) {
            // Only attempt connection if WiFi is connected
            if (!wifi_manager_is_sta_connected()) {
                // WiFi not connected, skip connection attempt
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            
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
                    ntrip_connection_start = time(NULL);
                    last_gga_time = -1; // Set to -1 to trigger immediate GGA send on first message
                    ESP_LOGI(TAG, "Successfully connected to NTRIP caster, waiting for first GGA");
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
            // Check if WiFi is still connected before attempting to read
            if (!wifi_manager_is_sta_connected()) {
                ESP_LOGW(TAG, "WiFi disconnected, marking NTRIP as disconnected");
                client->disconnect();
                ntrip_connected = false;
                reconnect_needed = true;
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            
            // Check for incoming RTCM data
            if (client->available() > 0) {
                rtcm_data_t rtcm_msg;
                int bytes_read = client->readData(rtcm_msg.data, sizeof(rtcm_msg.data));
                
                if (bytes_read < 0) {
                    // Read error - connection lost
                    ESP_LOGW(TAG, "Read error, marking connection as lost");
                    if (ntrip_connected && ntrip_connection_start > 0) {
                        ntrip_uptime_accumulated += (time(NULL) - ntrip_connection_start);
                    }
                    ntrip_connected = false;
                    reconnect_needed = true;
                } else if (bytes_read > 0) {
                    rtcm_msg.length = bytes_read;
                    
                    // Update statistics (assume 1 message per read for now)
                    statistics_rtcm_received(bytes_read, 1);
                    
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
                // Send immediately if this is the first GGA (last_gga_time == -1)
                // or if the interval has elapsed
                int64_t now = esp_timer_get_time();
                int64_t time_since_last_gga = (last_gga_time == -1) ? INT64_MAX : (now - last_gga_time) / 1000000;
                
                if (last_gga_time == -1 || time_since_last_gga >= ntrip_config.gga_interval_sec) {
                    client->sendGGA(gga_msg.sentence);
                    last_gga_time = now;
                    if (time_since_last_gga == INT64_MAX) {
                        ESP_LOGI(TAG, "Sent first GGA to NTRIP server, starting %d sec interval: %s", 
                                 ntrip_config.gga_interval_sec, gga_msg.sentence);
                    } else {
                        ESP_LOGI(TAG, "Sent GGA to NTRIP server: %s", gga_msg.sentence);
                    }
                } else {
                    ESP_LOGD(TAG, "GGA received but interval not elapsed yet (%lld/%d sec)", 
                             time_since_last_gga, ntrip_config.gga_interval_sec);
                }
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

uint32_t ntrip_get_uptime_sec(void) {
    if (ntrip_connected && ntrip_connection_start > 0) {
        return ntrip_uptime_accumulated + (time(NULL) - ntrip_connection_start);
    }
    return ntrip_uptime_accumulated;
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
