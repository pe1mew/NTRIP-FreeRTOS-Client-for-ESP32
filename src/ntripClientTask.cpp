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

#include "ledIndicatorTask.h"

static const char* TAG = "NTRIPTask";

// Queue handles (global, accessible from other tasks)
QueueHandle_t rtcm_queue = NULL;
QueueHandle_t gga_queue = NULL;

// Task handle
static TaskHandle_t ntrip_task_handle = NULL;

// Loop-time tracking (measured at start of each main-loop iteration; the
// stats task queries the average periodically and resets).
static int64_t ntrip_prev_loop_us = 0;
static uint64_t ntrip_loop_time_sum_us = 0;
static uint32_t ntrip_loop_count = 0;

extern "C" {
    // Exported for statisticsTask.cpp — returns the task handle (NULL if not started).
    TaskHandle_t ntrip_task_get_handle(void) {
        return ntrip_task_handle;
    }
    // Returns the running-average loop period in microseconds and resets the
    // accumulator. Returns 0 if no samples since the last reset.
    uint32_t ntrip_task_get_avg_loop_us_and_reset(void) {
        uint32_t avg = (ntrip_loop_count > 0)
            ? (uint32_t)(ntrip_loop_time_sum_us / ntrip_loop_count) : 0;
        ntrip_loop_time_sum_us = 0;
        ntrip_loop_count = 0;
        return avg;
    }
}

// Connection state
static bool ntrip_connected = false;
static time_t ntrip_connection_start = 0;
static uint32_t ntrip_uptime_accumulated = 0;

// RTCM quality monitoring
static uint64_t last_rtcm_time = 0;
#define RTCM_GAP_THRESHOLD_MS 5000  // 5 seconds without RTCM = data gap

// Reference station position tracking
typedef struct {
    double ecef_x;      // ECEF X coordinate (meters)
    double ecef_y;      // ECEF Y coordinate (meters)
    double ecef_z;      // ECEF Z coordinate (meters)
    uint64_t last_update_time;  // Timestamp of last update
    bool valid;         // Whether we have valid reference station data
} reference_station_t;

static reference_station_t reference_station = {0, 0, 0, 0, false};

// RTCM frame parser state
#define RTCM_BUFFER_SIZE 2048
static uint8_t rtcm_buffer[RTCM_BUFFER_SIZE];
static size_t rtcm_buffer_len = 0;

// Queue configuration
#define RTCM_QUEUE_LENGTH   10  // Can buffer 10 RTCM messages
#define GGA_QUEUE_LENGTH    5   // Can buffer 5 GGA sentences

// Task configuration
#define NTRIP_TASK_STACK_SIZE   8192
#define NTRIP_TASK_PRIORITY     3

/**
 * @brief Extract bits from byte array (MSB first)
 */
static uint64_t extract_bits(const uint8_t* data, uint32_t bit_offset, uint8_t num_bits) {
    uint64_t value = 0;
    for (uint8_t i = 0; i < num_bits; i++) {
        uint32_t byte_idx = (bit_offset + i) / 8;
        uint8_t bit_idx = 7 - ((bit_offset + i) % 8);
        if (data[byte_idx] & (1 << bit_idx)) {
            value |= (1ULL << (num_bits - 1 - i));
        }
    }
    return value;
}

/**
 * @brief Process accumulated RTCM data and extract reference station coordinates from message 1005
 * @param incoming_data New data from NTRIP stream
 * @param incoming_len Length of new data
 * @return true if RTCM 1005 was found and parsed
 */
static bool parse_rtcm_1005(const uint8_t* incoming_data, size_t incoming_len) {
    bool found_1005 = false;
    
    // Append incoming data to buffer
    if (rtcm_buffer_len + incoming_len > RTCM_BUFFER_SIZE) {
        // Buffer full - shift or reset
        if (incoming_len < RTCM_BUFFER_SIZE) {
            // Keep most recent data
            size_t keep_len = RTCM_BUFFER_SIZE - incoming_len;
            memmove(rtcm_buffer, rtcm_buffer + (rtcm_buffer_len - keep_len), keep_len);
            rtcm_buffer_len = keep_len;
        } else {
            // Incoming data too large, reset buffer
            rtcm_buffer_len = 0;
        }
    }
    
    memcpy(rtcm_buffer + rtcm_buffer_len, incoming_data, incoming_len);
    rtcm_buffer_len += incoming_len;
    
    // Scan buffer for RTCM frames
    size_t i = 0;
    while (i < rtcm_buffer_len) {
        // Look for sync byte 0xD3
        if (rtcm_buffer[i] != 0xD3) {
            i++;
            continue;
        }
        
        // Need at least 3 bytes for header
        if (i + 3 > rtcm_buffer_len) {
            break;  // Wait for more data
        }
        
        // Extract message length (10 bits, starting at bit 14)
        uint16_t msg_len = (uint16_t)extract_bits(&rtcm_buffer[i], 14, 10);
        
        // Total frame length = 3 (header) + msg_len + 3 (CRC24)
        size_t frame_len = 3 + msg_len + 3;
        
        // Check if we have the complete frame
        if (i + frame_len > rtcm_buffer_len) {
            break;  // Wait for more data
        }
        
        // Extract message ID (12 bits at bit offset 24 from frame start)
        uint16_t msg_id = (uint16_t)extract_bits(&rtcm_buffer[i], 24, 12);
        
        // Process message 1005 (reference station coordinates)
        if (msg_id == 1005) {
            // RTCM 1005: Header(24) + MsgID(12) + StationID(12) + ITRF(6) + Indicators(4) + ECEF-X(38) + ... 
            // ECEF-X starts at bit 58 from frame start
            uint32_t bit_pos = 58;
            
            // Extract ECEF X (38 bits, signed)
            uint64_t ecef_x_raw = extract_bits(&rtcm_buffer[i], bit_pos, 38);
            int64_t ecef_x = (ecef_x_raw & (1ULL << 37)) ? 
                (int64_t)(ecef_x_raw | 0xFFFFFFC000000000ULL) : (int64_t)ecef_x_raw;
            bit_pos += 38 + 2;
            
            // Extract ECEF Y (38 bits, signed)
            uint64_t ecef_y_raw = extract_bits(&rtcm_buffer[i], bit_pos, 38);
            int64_t ecef_y = (ecef_y_raw & (1ULL << 37)) ? 
                (int64_t)(ecef_y_raw | 0xFFFFFFC000000000ULL) : (int64_t)ecef_y_raw;
            bit_pos += 38 + 2;
            
            // Extract ECEF Z (38 bits, signed)
            uint64_t ecef_z_raw = extract_bits(&rtcm_buffer[i], bit_pos, 38);
            int64_t ecef_z = (ecef_z_raw & (1ULL << 37)) ? 
                (int64_t)(ecef_z_raw | 0xFFFFFFC000000000ULL) : (int64_t)ecef_z_raw;
            
            // Convert from 0.0001 meter units to meters
            reference_station.ecef_x = ecef_x * 0.0001;
            reference_station.ecef_y = ecef_y * 0.0001;
            reference_station.ecef_z = ecef_z * 0.0001;
            reference_station.last_update_time = esp_timer_get_time();
            reference_station.valid = true;
            
            ESP_LOGI(TAG, "RTCM 1005: Reference station ECEF X=%.2f, Y=%.2f, Z=%.2f m", 
                     reference_station.ecef_x, reference_station.ecef_y, reference_station.ecef_z);
            
            found_1005 = true;
        }
        
        // Move to next potential frame
        i += frame_len;
    }
    
    // Remove processed data from buffer
    if (i > 0) {
        size_t remaining = rtcm_buffer_len - i;
        if (remaining > 0) {
            memmove(rtcm_buffer, rtcm_buffer + i, remaining);
        }
        rtcm_buffer_len = remaining;
    }
    
    return found_1005;
}

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
        // Sample loop period (start-to-start) for the avg-loop-time stat.
        int64_t now_loop_us = esp_timer_get_time();
        if (ntrip_prev_loop_us != 0) {
            ntrip_loop_time_sum_us += (uint64_t)(now_loop_us - ntrip_prev_loop_us);
            ntrip_loop_count++;
        }
        ntrip_prev_loop_us = now_loop_us;

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

                // Stamp the reconnect start for ntrip_avg_reconnect_time_ms.
                // The matching CONNECTED below also increments ntrip_reconnect_count.
                statistics_ntrip_event(NTRIP_STATS_EVENT_RECONNECT_BEGIN);

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
                    statistics_ntrip_event(NTRIP_STATS_EVENT_CONNECTED);
                    ESP_LOGI(TAG, "Successfully connected to NTRIP caster, waiting for first GGA");
                } else {
                    ESP_LOGW(TAG, "Failed to connect to NTRIP caster, will retry in %d seconds",
                             ntrip_config.reconnect_delay_sec);
                    client->disconnect();
                    ntrip_connected = false;
                    // Best-effort auth-vs-other distinction: NTRIPClient logs the
                    // status line ("NTRIP HTTP non-200: HTTP/1.1 401 Unauthorized").
                    // We don't have an API to read the code back; treating every
                    // failed connect as a generic disconnect is acceptable. If you
                    // need auth-failure counts, add a getLastHttpStatus() to NTRIPClient.
                    statistics_ntrip_event(NTRIP_STATS_EVENT_DISCONNECTED);
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
                statistics_ntrip_event(NTRIP_STATS_EVENT_DISCONNECTED);
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            
            // Check for incoming RTCM data
            if (client->available() > 0) {
                // Check for data gaps
                uint64_t now = esp_timer_get_time();
                if (last_rtcm_time != 0 && (now - last_rtcm_time) > (RTCM_GAP_THRESHOLD_MS * 1000ULL)) {
                    uint32_t gap_sec = (uint32_t)((now - last_rtcm_time) / 1000000ULL);
                    ESP_LOGW(TAG, "RTCM data gap detected: %.1f sec", (now - last_rtcm_time) / 1000000.0);
                    statistics_rtcm_data_gap(gap_sec);
                }
                
                rtcm_data_t rtcm_msg;
                int bytes_read = client->readData(rtcm_msg.data, sizeof(rtcm_msg.data));
                
                if (bytes_read < 0) {
                    // Read error - connection lost
                    ESP_LOGW(TAG, "Read error, marking connection as lost");
                    statistics_ntrip_timeout();
                    if (ntrip_connected && ntrip_connection_start > 0) {
                        ntrip_uptime_accumulated += (time(NULL) - ntrip_connection_start);
                    }
                    ntrip_connected = false;
                    reconnect_needed = true;
                    statistics_ntrip_event(NTRIP_STATS_EVENT_DISCONNECTED);
                } else if (bytes_read > 0) {
                    rtcm_msg.length = bytes_read;
                    rtcm_msg.receive_timestamp = esp_timer_get_time();
                    last_rtcm_time = rtcm_msg.receive_timestamp;
                    
                    // Parse RTCM stream for message 1005 (reference station position)
                    parse_rtcm_1005(rtcm_msg.data, bytes_read);
                    
                    // Update statistics (assume 1 message per read for now)
                    statistics_rtcm_received(bytes_read, 1);
                    
                    // Notify LED task of RTCM data activity
                    led_update_ntrip_activity();
                    
                    // Send to GNSS via queue (ring buffer behavior - drop oldest if full)
                    if (xQueueSend(rtcm_queue, &rtcm_msg, 0) != pdTRUE) {
                        // Queue full - remove oldest item and add new one (ring buffer)
                        rtcm_data_t dummy;
                        if (xQueueReceive(rtcm_queue, &dummy, 0) == pdTRUE) {
                            statistics_rtcm_queue_overflow();
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
                    // Only log task-level success if sendGGA didn't flip the
                    // connected flag on a write failure. Otherwise the log
                    // line lies: we'd say "Sent GGA to NTRIP server" while the
                    // caster's TCP RST means nothing actually reached it.
                    if (client->isConnected()) {
                        if (time_since_last_gga == INT64_MAX) {
                            ESP_LOGI(TAG, "Sent first GGA to NTRIP server, starting %d sec interval: %s",
                                     ntrip_config.gga_interval_sec, gga_msg.sentence);
                        } else {
                            ESP_LOGI(TAG, "Sent GGA to NTRIP server: %s", gga_msg.sentence);
                        }
                    }
                    // If sendGGA failed and cleared connected_flag, the
                    // "Connection lost" check at the end of this iteration
                    // (`if (!client->isConnected())`) will set reconnect_needed
                    // and the next loop iteration takes the reconnect path.
                } else {
                    ESP_LOGD(TAG, "GGA received but interval not elapsed yet (%lld/%d sec)", 
                             time_since_last_gga, ntrip_config.gga_interval_sec);
                }
            }
            // Note: previously there was an "else" branch here that updated
            // last_gga_time whenever the interval elapsed with an empty queue.
            // That was a silent bug: it suppressed *every* subsequent GGA send,
            // because the interval check on the next dequeued GGA computed
            // time_since_last_gga against the reset (never-sent) timestamp and
            // returned "interval not elapsed yet". Removed entirely — the only
            // way last_gga_time advances is via an actual sendGGA() above.
            
            // Verify connection is still active
            if (!client->isConnected()) {
                ESP_LOGW(TAG, "Connection lost, will attempt reconnect");
                ntrip_connected = false;
                reconnect_needed = true;
                statistics_ntrip_event(NTRIP_STATS_EVENT_DISCONNECTED);
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

bool ntrip_get_reference_station_ecef(double* ecef_x, double* ecef_y, double* ecef_z) {
    if (!reference_station.valid || ecef_x == NULL || ecef_y == NULL || ecef_z == NULL) {
        return false;
    }
    *ecef_x = reference_station.ecef_x;
    *ecef_y = reference_station.ecef_y;
    *ecef_z = reference_station.ecef_z;
    return true;
}

uint32_t ntrip_get_uptime_sec(void) {
    if (ntrip_connected && ntrip_connection_start > 0) {
        return ntrip_uptime_accumulated + (time(NULL) - ntrip_connection_start);
    }
    return ntrip_uptime_accumulated;
}

