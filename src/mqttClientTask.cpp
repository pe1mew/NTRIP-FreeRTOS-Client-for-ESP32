#include "mqttClientTask.h"
#include "configurationManagerTask.h"
#include "gnssReceiverTask.h"
#include "statisticsTask.h"
#include "wifiManager.h"
#include "ntripClientTask.h"

#include "ledIndicatorTask.h"

#include <string.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mqtt_client.h"

static const char *TAG = "MQTT_CLIENT";

// Task handle
static TaskHandle_t mqtt_task_handle = NULL;

// MQTT client handle
static esp_mqtt_client_handle_t mqtt_client = NULL;

// Status tracking
static bool mqtt_connected = false;
static uint32_t message_counter = 0;
static uint32_t total_published = 0;
static time_t mqtt_connection_start = 0;
static uint32_t mqtt_uptime_accumulated = 0;
static time_t last_activity_time = 0;

// Forward declarations
static void mqtt_task(void *pvParameters);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void format_gnss_json(const mqtt_gnss_message_t *msg, char *buffer, size_t size);
static void format_status_json(const mqtt_status_message_t *msg, char *buffer, size_t size);
static void format_stats_json(const mqtt_stats_message_t *msg, char *buffer, size_t size);
static void collect_system_status(mqtt_status_message_t *msg);
static void collect_period_statistics(mqtt_stats_message_t *msg);

// Initialize MQTT client task
esp_err_t mqtt_client_task_init(void) {
    ESP_LOGI(TAG, "Initializing MQTT client task");
    
    // Create MQTT task
    BaseType_t result = xTaskCreate(
        mqtt_task,
        "mqtt_client",
        5120,  // Stack size (increased for JSON buffers)
        NULL,
        2,     // Priority (lower than critical tasks)
        &mqtt_task_handle
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create MQTT client task");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "MQTT client task created successfully");
    return ESP_OK;
}

// Stop MQTT client task
void mqtt_client_task_stop(void) {
    if (mqtt_client) {
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
    }
    
    if (mqtt_task_handle) {
        vTaskDelete(mqtt_task_handle);
        mqtt_task_handle = NULL;
    }
    
    ESP_LOGI(TAG, "MQTT client task stopped");
}

// Check connection status
bool mqtt_is_connected(void) {
    return mqtt_connected;
}

// Get publish count
uint32_t mqtt_get_publish_count(void) {
    return total_published;
}

// Get MQTT uptime
uint32_t mqtt_get_uptime_sec(void) {
    uint32_t uptime = mqtt_uptime_accumulated;
    
    // Add current session time if connected
    if (mqtt_connected && mqtt_connection_start > 0) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        uptime += (tv.tv_sec - mqtt_connection_start);
    }
    
    return uptime;
}

// Set last activity time
void mqtt_set_last_activity_time(time_t timestamp) {
    last_activity_time = timestamp;
}

// Get last activity time
time_t mqtt_get_last_activity_time(void) {
    return last_activity_time;
}

// MQTT event handler
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected to broker");
            mqtt_connected = true;
            
            struct timeval tv;
            gettimeofday(&tv, NULL);
            mqtt_connection_start = tv.tv_sec;
            
            // Update activity time for LED indicator
            mqtt_set_last_activity_time(mqtt_connection_start);
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected from broker");
            
            // Accumulate uptime before disconnecting
            if (mqtt_connection_start > 0) {
                gettimeofday(&tv, NULL);
                mqtt_uptime_accumulated += (tv.tv_sec - mqtt_connection_start);
                mqtt_connection_start = 0;
            }
            
            mqtt_connected = false;
            break;
            
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT message published, msg_id=%d", event->msg_id);
            
            // Update activity time for LED indicator
            gettimeofday(&tv, NULL);
            mqtt_set_last_activity_time(tv.tv_sec);
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error: type=%d", event->error_handle->error_type);
            break;
            
        default:
            break;
    }
}

// Main MQTT task
static void mqtt_task(void *pvParameters) {
    mqtt_config_t config;
    uint32_t gnss_counter = 0;
    uint32_t status_counter = 0;
    uint32_t stats_counter = 0;
    
    ESP_LOGI(TAG, "MQTT client task started");
    
    // Get configuration event group for monitoring changes
    EventGroupHandle_t config_events = config_get_event_group();
    
    // Load configuration
    if (config_manager_get_mqtt_config(&config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load MQTT configuration");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "MQTT enabled: %s", config.enabled ? "true" : "false");
    
    // Only initialize if enabled
    if (!config.enabled) {
        ESP_LOGI(TAG, "MQTT is disabled, waiting for enable...");
        mqtt_client = NULL;
    }

    int64_t last_config_poll = 0;

    // If enabled at boot, start client
    if (config.enabled) {
        char broker_uri[256];
        snprintf(broker_uri, sizeof(broker_uri), "mqtt://%s:%d", config.broker, config.port);
        ESP_LOGI(TAG, "Connecting to MQTT broker: %s", broker_uri);
        ESP_LOGI(TAG, "Base topic: %s", config.topic);
        ESP_LOGI(TAG, "Intervals - GNSS: %u sec, Status: %u sec, Stats: %u sec",
                 config.gnss_interval_sec, config.status_interval_sec, config.stats_interval_sec);
        esp_mqtt_client_config_t mqtt_cfg = {};
        mqtt_cfg.broker.address.uri = broker_uri;
        mqtt_cfg.credentials.username = config.user;
        mqtt_cfg.credentials.authentication.password = config.password;
        mqtt_cfg.session.keepalive = 60;
        mqtt_cfg.session.disable_clean_session = false;
        mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
        if (mqtt_client != NULL) {
            esp_mqtt_client_register_event(mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
            esp_mqtt_client_start(mqtt_client);
            ESP_LOGI(TAG, "MQTT client enabled and started");
        }
    }
    
    // Main loop
    while (1) {
        // Check for configuration changes (non-blocking)
        EventBits_t bits = xEventGroupGetBits(config_events);
        if (bits & CONFIG_MQTT_CHANGED_BIT) {
            // Clear the bit
            xEventGroupClearBits(config_events, CONFIG_MQTT_CHANGED_BIT);
            
            // Reload configuration
            mqtt_config_t new_config;
            if (config_manager_get_mqtt_config(&new_config) == ESP_OK) {
                // Check if enabled state changed
                if (new_config.enabled != config.enabled) {
                    ESP_LOGI(TAG, "MQTT enabled changed: %s -> %s", 
                             config.enabled ? "true" : "false",
                             new_config.enabled ? "true" : "false");
                    
                    if (new_config.enabled && mqtt_client == NULL) {
                        // Enable MQTT - create and start client
                        ESP_LOGI(TAG, "Enabling MQTT client...");
                        
                        // Build broker URI
                        char broker_uri[256];
                        snprintf(broker_uri, sizeof(broker_uri), "mqtt://%s:%d", new_config.broker, new_config.port);
                        
                        // Initialize MQTT client configuration
                        esp_mqtt_client_config_t mqtt_cfg = {};
                        mqtt_cfg.broker.address.uri = broker_uri;
                        mqtt_cfg.credentials.username = new_config.user;
                        mqtt_cfg.credentials.authentication.password = new_config.password;
                        mqtt_cfg.session.keepalive = 60;
                        mqtt_cfg.session.disable_clean_session = false;
                        
                        mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
                        if (mqtt_client != NULL) {
                            esp_mqtt_client_register_event(mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
                            esp_mqtt_client_start(mqtt_client);
                            ESP_LOGI(TAG, "MQTT client enabled and started");
                        }
                    } else if (!new_config.enabled && mqtt_client != NULL) {
                        // Disable MQTT - stop and destroy client
                        ESP_LOGI(TAG, "Disabling MQTT client...");
                        esp_mqtt_client_stop(mqtt_client);
                        esp_mqtt_client_destroy(mqtt_client);
                        mqtt_client = NULL;
                        mqtt_connected = false;
                        ESP_LOGI(TAG, "MQTT client disabled");
                        
                        // Reset counters
                        gnss_counter = 0;
                        status_counter = 0;
                        stats_counter = 0;
                    }
                }
                
                // Check if intervals changed
                if (new_config.gnss_interval_sec != config.gnss_interval_sec ||
                    new_config.status_interval_sec != config.status_interval_sec ||
                    new_config.stats_interval_sec != config.stats_interval_sec) {
                    
                    ESP_LOGI(TAG, "MQTT intervals updated - GNSS: %u sec, Status: %u sec, Stats: %u sec",
                             new_config.gnss_interval_sec, new_config.status_interval_sec, 
                             new_config.stats_interval_sec);
                    
                    // Reset counters when intervals change
                    gnss_counter = 0;
                    status_counter = 0;
                    stats_counter = 0;
                }
                
                // Update config (note: broker/topic changes require restart for simplicity)
                config = new_config;
            }
        }
        

        // 1 second tick for interval management
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Periodic config poll to catch missed event bits (runtime toggle)
        int64_t now_us = esp_timer_get_time();
        if ((now_us - last_config_poll) >= 1000000) {
            last_config_poll = now_us;
            mqtt_config_t polled_config;
            if (config_manager_get_mqtt_config(&polled_config) == ESP_OK) {
                if (polled_config.enabled != config.enabled) {
                    ESP_LOGI(TAG, "MQTT enabled changed via poll: %s -> %s",
                             config.enabled ? "true" : "false",
                             polled_config.enabled ? "true" : "false");
                    config = polled_config;
                    if (!config.enabled && mqtt_client != NULL) {
                        ESP_LOGI(TAG, "Polling detected disable, disconnecting MQTT");
                        esp_mqtt_client_stop(mqtt_client);
                        esp_mqtt_client_destroy(mqtt_client);
                        mqtt_client = NULL;
                        mqtt_connected = false;
                        gnss_counter = 0;
                        status_counter = 0;
                        stats_counter = 0;
                    } else if (config.enabled && mqtt_client == NULL) {
                        ESP_LOGI(TAG, "Polling detected enable, starting MQTT");
                        char broker_uri[256];
                        snprintf(broker_uri, sizeof(broker_uri), "mqtt://%s:%d", config.broker, config.port);
                        esp_mqtt_client_config_t mqtt_cfg = {};
                        mqtt_cfg.broker.address.uri = broker_uri;
                        mqtt_cfg.credentials.username = config.user;
                        mqtt_cfg.credentials.authentication.password = config.password;
                        mqtt_cfg.session.keepalive = 60;
                        mqtt_cfg.session.disable_clean_session = false;
                        mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
                        if (mqtt_client != NULL) {
                            esp_mqtt_client_register_event(mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
                            esp_mqtt_client_start(mqtt_client);
                            ESP_LOGI(TAG, "MQTT client enabled and started (poll)");
                        }
                    }
                } else {
                    config = polled_config;
                }
            }
        }

        // Skip if not connected
        if (!mqtt_connected) {
            // Reset counters when disconnected to sync on reconnect
            gnss_counter = 0;
            status_counter = 0;
            stats_counter = 0;
            continue;
        }
        
        // Increment counters only if their intervals are enabled (> 0)
        if (config.gnss_interval_sec > 0) gnss_counter++;
        if (config.status_interval_sec > 0) status_counter++;
        if (config.stats_interval_sec > 0) stats_counter++;
        
        ESP_LOGD(TAG, "Counters - GNSS:%lu/%u, Status:%lu/%u, Stats:%lu/%u", 
                 gnss_counter, config.gnss_interval_sec,
                 status_counter, config.status_interval_sec,
                 stats_counter, config.stats_interval_sec);
        
        // Publish GNSS position data (only if interval > 0)
        if (config.gnss_interval_sec > 0 && gnss_counter >= config.gnss_interval_sec) {
            gnss_counter = 0;
            
            gnss_data_t gnss_data;
            gnss_get_data(&gnss_data);
            if (gnss_data.valid) {
                mqtt_gnss_message_t gnss_msg = {};
                
                // Map pre-parsed data to MQTT message structure
                gnss_msg.lat = gnss_data.latitude;
                gnss_msg.lon = gnss_data.longitude;
                gnss_msg.alt = gnss_data.altitude;
                gnss_msg.fix_type = gnss_data.fix_quality;
                gnss_msg.speed = gnss_data.speed;
                gnss_msg.dir = gnss_data.heading;
                gnss_msg.sats = gnss_data.satellites;
                gnss_msg.hdop = gnss_data.hdop;
                gnss_msg.age = gnss_data.dgps_age;  // Age of differential data from GGA field 13
                
                // Format timestamp as ISO 8601
                snprintf(gnss_msg.daytime, sizeof(gnss_msg.daytime),
                         "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                         2000 + gnss_data.year, gnss_data.month, gnss_data.day,
                         gnss_data.hour, gnss_data.minute, gnss_data.second, gnss_data.millisecond);
                gnss_msg.num = ++message_counter;
                
                // Format and publish
                char json_buffer[512];
                format_gnss_json(&gnss_msg, json_buffer, sizeof(json_buffer));
                
                char topic[128];
                snprintf(topic, sizeof(topic), "%s/GNSS", config.topic);
                
                int msg_id = esp_mqtt_client_publish(mqtt_client, topic, json_buffer, 0, 0, 0);
                if (msg_id >= 0) {
                    total_published++;
                    led_update_mqtt_activity();  // Blink LED on publish
                    ESP_LOGI(TAG, "Published GNSS #%lu to %s", message_counter, topic);
                } else {
                    ESP_LOGE(TAG, "Failed to publish GNSS message");
                }
            } else {
                ESP_LOGW(TAG, "No valid GNSS data, skipping GNSS publish");
            }
        }
        
        // Publish system status (cumulative runtime data, only if interval > 0)
        if (config.status_interval_sec > 0 && status_counter >= config.status_interval_sec) {
            status_counter = 0;
            
            mqtt_status_message_t status_msg = {};
            collect_system_status(&status_msg);
            
            char json_buffer[1024];
            format_status_json(&status_msg, json_buffer, sizeof(json_buffer));
            
            char topic[128];
            snprintf(topic, sizeof(topic), "%s/status", config.topic);
            
            int msg_id = esp_mqtt_client_publish(mqtt_client, topic, json_buffer, 0, 0, 0);
            if (msg_id >= 0) {
                total_published++;
                led_update_mqtt_activity();  // Blink LED on publish
                ESP_LOGI(TAG, "Published system status to %s", topic);
            } else {
                ESP_LOGE(TAG, "Failed to publish status message");
            }
        }
        
        // Publish period statistics (only if interval > 0)
        if (config.stats_interval_sec > 0 && stats_counter >= config.stats_interval_sec) {
            stats_counter = 0;
            
            mqtt_stats_message_t stats_msg = {};
            collect_period_statistics(&stats_msg);
            
            char json_buffer[1024];
            format_stats_json(&stats_msg, json_buffer, sizeof(json_buffer));
            
            char topic[128];
            snprintf(topic, sizeof(topic), "%s/stats", config.topic);
            
            int msg_id = esp_mqtt_client_publish(mqtt_client, topic, json_buffer, 0, 0, 0);
            if (msg_id >= 0) {
                total_published++;
                led_update_mqtt_activity();  // Blink LED on publish
                ESP_LOGI(TAG, "Published statistics to %s", topic);
            } else {
                ESP_LOGE(TAG, "Failed to publish stats message");
            }
        }
    }
}

// Collect system status
static void collect_system_status(mqtt_status_message_t *msg) {
    // Get GNSS data for timestamp
    gnss_data_t gnss_data;
    gnss_get_data(&gnss_data);
    
    // Format timestamp from GNSS time (UTC)
    if (gnss_data.valid && gnss_data.year > 0) {
        snprintf(msg->timestamp, sizeof(msg->timestamp),
                 "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                 2000 + gnss_data.year, gnss_data.month, gnss_data.day,
                 gnss_data.hour, gnss_data.minute, gnss_data.second, gnss_data.millisecond);
    } else {
        snprintf(msg->timestamp, sizeof(msg->timestamp), "NO_GNSS_TIME");
    }
    
    // System metrics
    msg->uptime_sec = esp_timer_get_time() / 1000000;
    msg->heap_free = esp_get_free_heap_size();
    msg->heap_min = esp_get_minimum_free_heap_size();
    
    // WiFi status
    msg->wifi_connected = wifi_manager_is_sta_connected();
    msg->wifi_rssi = 0;
    if (msg->wifi_connected) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            msg->wifi_rssi = ap_info.rssi;
        }
    }
    
    // NTRIP status (from Statistics Task runtime data)
    runtime_statistics_t runtime_stats;
    statistics_get_runtime(&runtime_stats);
    msg->ntrip_connected = ntrip_is_connected();
    msg->ntrip_uptime_sec = runtime_stats.ntrip_uptime_sec;
    msg->ntrip_reconnects = runtime_stats.ntrip_reconnect_count;
    msg->rtcm_packets_total = runtime_stats.rtcm_messages_received_total;
    
    // MQTT status
    msg->mqtt_connected = mqtt_connected;
    msg->mqtt_uptime_sec = mqtt_get_uptime_sec();
    msg->mqtt_published = total_published;
    
    // WiFi reconnects
    msg->wifi_reconnects = runtime_stats.wifi_reconnect_count_total;
    
    // GNSS status
    msg->current_fix = 0;
    if (gnss_data.valid) {
        msg->current_fix = gnss_data.fix_quality;
    }
}

// Collect period statistics
static void collect_period_statistics(mqtt_stats_message_t *msg) {
    // Get GNSS data for timestamp
    gnss_data_t gnss_data;
    gnss_get_data(&gnss_data);
    
    // Format timestamp from GNSS time (UTC)
    if (gnss_data.valid && gnss_data.year > 0) {
        snprintf(msg->timestamp, sizeof(msg->timestamp),
                 "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                 2000 + gnss_data.year, gnss_data.month, gnss_data.day,
                 gnss_data.hour, gnss_data.minute, gnss_data.second, gnss_data.millisecond);
    } else {
        snprintf(msg->timestamp, sizeof(msg->timestamp), "NO_GNSS_TIME");
    }
    
    // Query Statistics Task for period data
    period_statistics_t period_stats;
    statistics_get_period(&period_stats);
    
    // Query runtime data for time-to-fix metrics
    runtime_statistics_t runtime_stats;
    statistics_get_runtime(&runtime_stats);
    
    msg->period_duration = 60;  // Default, will be overridden by actual period
    
    // RTCM metrics
    msg->rtcm_bytes_received = period_stats.rtcm_bytes_received;
    msg->rtcm_message_rate = period_stats.rtcm_message_rate;
    msg->rtcm_data_gaps = period_stats.rtcm_data_gaps;
    msg->rtcm_avg_latency_ms = period_stats.rtcm_avg_latency_ms;
    msg->rtcm_corrupted = period_stats.rtcm_corrupted_count;
    
    // GNSS metrics
    memcpy(msg->fix_quality_duration, period_stats.fix_quality_duration, sizeof(msg->fix_quality_duration));
    msg->rtk_fixed_percent = period_stats.rtk_fixed_stability_percent;
    msg->time_to_rtk_fixed_sec = runtime_stats.time_to_rtk_fixed_sec;
    msg->fix_downgrades = period_stats.fix_downgrades;
    msg->fix_upgrades = period_stats.fix_upgrades;
    msg->hdop_avg = period_stats.hdop_avg;
    msg->hdop_min = period_stats.hdop_min;
    msg->hdop_max = period_stats.hdop_max;
    msg->sats_avg = period_stats.satellites_avg;
    msg->baseline_distance_km = period_stats.baseline_distance_km;
    
    // GGA transmission
    msg->gga_sent_count = period_stats.gga_sent_count;
    msg->gga_failures = period_stats.gga_send_failures;
    msg->gga_overflows = period_stats.gga_queue_overflows;
    
    // WiFi metrics
    msg->wifi_rssi_avg = period_stats.wifi_rssi_avg;
    msg->wifi_rssi_min = period_stats.wifi_rssi_min;
    msg->wifi_rssi_max = period_stats.wifi_rssi_max;
    msg->wifi_uptime_percent = period_stats.wifi_uptime_percent;
    
    // System metrics
    msg->gnss_update_rate_hz = period_stats.gnss_update_rate_hz;
    
    // Error metrics
    msg->nmea_errors = period_stats.nmea_checksum_errors;
    msg->uart_errors = period_stats.uart_errors;
    msg->rtcm_queue_overflows = period_stats.rtcm_queue_overflows;
    msg->ntrip_timeouts = period_stats.ntrip_timeouts;
}

// Format GNSS JSON message
static void format_gnss_json(const mqtt_gnss_message_t *msg, char *buffer, size_t size) {
    snprintf(buffer, size,
        "{\n"
        "   \"num\": %lu,\n"
        "   \"daytime\": \"%s\",\n"
        "   \"lat\": %.7f,\n"
        "   \"lon\": %.7f,\n"
        "   \"alt\": %.3f,\n"
        "   \"fix_type\": %u,\n"
        "   \"speed\": %.2f,\n"
        "   \"dir\": %.1f,\n"
        "   \"sats\": %u,\n"
        "   \"hdop\": %.2f,\n"
        "   \"age\": %.2f\n"
        "}",
        msg->num,
        msg->daytime,
        msg->lat,
        msg->lon,
        msg->alt,
        msg->fix_type,
        msg->speed,
        msg->dir,
        msg->sats,
        msg->hdop,
        (double)msg->age  // Explicit cast to double for snprintf
    );
}

// Format system status JSON message
static void format_status_json(const mqtt_status_message_t *msg, char *buffer, size_t size) {
    snprintf(buffer, size,
        "{\n"
        "   \"timestamp\": \"%s\",\n"
        "   \"uptime_sec\": %lu,\n"
        "   \"heap_free\": %lu,\n"
        "   \"heap_min\": %lu,\n"
        "   \"wifi\": {\n"
        "      \"rssi_dbm\": %d,\n"
        "      \"reconnects\": %lu\n"
        "   },\n"
        "   \"ntrip\": {\n"
        "      \"connected\": %s,\n"
        "      \"uptime_sec\": %lu,\n"
        "      \"reconnects\": %lu,\n"
        "      \"rtcm_packets_total\": %lu\n"
        "   },\n"
        "   \"mqtt\": {\n"
        "      \"uptime_sec\": %lu,\n"
        "      \"messages_published\": %lu\n"
        "   },\n"
        "   \"gnss\": {\n"
        "      \"current_fix\": %u\n"
        "   }\n"
        "}",
        msg->timestamp,
        msg->uptime_sec,
        msg->heap_free,
        msg->heap_min,
        msg->wifi_rssi,
        msg->wifi_reconnects,
        msg->ntrip_connected ? "true" : "false",
        msg->ntrip_uptime_sec,
        msg->ntrip_reconnects,
        msg->rtcm_packets_total,
        msg->mqtt_uptime_sec,
        msg->mqtt_published,
        msg->current_fix
    );
}

// Format statistics JSON message
static void format_stats_json(const mqtt_stats_message_t *msg, char *buffer, size_t size) {
    snprintf(buffer, size,
        "{\n"
        "   \"timestamp\": \"%s\",\n"
        "   \"period_sec\": %lu,\n"
        "   \"rtcm\": {\n"
        "      \"bytes_received\": %lu,\n"
        "      \"message_rate\": %lu,\n"
        "      \"data_gaps\": %lu,\n"
        "      \"avg_latency_ms\": %lu,\n"
        "      \"corrupted\": %lu\n"
        "   },\n"
        "   \"gnss\": {\n"
        "      \"fix_duration\": {\n"
        "         \"no_fix\": %lu,\n"
        "         \"gps\": %lu,\n"
        "         \"dgps\": %lu,\n"
        "         \"rtk_float\": %lu,\n"
        "         \"rtk_fixed\": %lu\n"
        "      },\n"
        "      \"rtk_fixed_percent\": %.1f,\n"
        "      \"time_to_rtk_fixed_sec\": %lu,\n"
        "      \"fix_downgrades\": %lu,\n"
        "      \"fix_upgrades\": %lu,\n"
        "      \"hdop_avg\": %.2f,\n"
        "      \"hdop_min\": %.2f,\n"
        "      \"hdop_max\": %.2f,\n"
        "      \"sats_avg\": %u,\n"
        "      \"baseline_distance_km\": %.2f,\n"
        "      \"update_rate_hz\": %lu\n"
        "   },\n"
        "   \"gga\": {\n"
        "      \"sent_count\": %lu,\n"
        "      \"failures\": %lu,\n"
        "      \"queue_overflows\": %lu\n"
        "   },\n"
        "   \"wifi\": {\n"
        "      \"rssi_avg\": %d,\n"
        "      \"rssi_min\": %d,\n"
        "      \"rssi_max\": %d,\n"
        "      \"uptime_percent\": %.1f\n"
        "   },\n"
        "   \"errors\": {\n"
        "      \"nmea_checksum\": %lu,\n"
        "      \"uart\": %lu,\n"
        "      \"rtcm_queue_overflow\": %lu,\n"
        "      \"ntrip_timeouts\": %lu\n"
        "   }\n"
        "}",
        msg->timestamp,
        msg->period_duration,
        msg->rtcm_bytes_received,
        msg->rtcm_message_rate,
        msg->rtcm_data_gaps,
        msg->rtcm_avg_latency_ms,
        msg->rtcm_corrupted,
        msg->fix_quality_duration[0],  // no_fix
        msg->fix_quality_duration[1],  // gps
        msg->fix_quality_duration[2],  // dgps
        msg->fix_quality_duration[5],  // rtk_float
        msg->fix_quality_duration[4],  // rtk_fixed
        msg->rtk_fixed_percent,
        msg->time_to_rtk_fixed_sec,
        msg->fix_downgrades,
        msg->fix_upgrades,
        msg->hdop_avg,
        msg->hdop_min,
        msg->hdop_max,
        msg->sats_avg,
        msg->baseline_distance_km,
        msg->gnss_update_rate_hz,
        msg->gga_sent_count,
        msg->gga_failures,
        msg->gga_overflows,
        msg->wifi_rssi_avg,
        msg->wifi_rssi_min,
        msg->wifi_rssi_max,
        msg->wifi_uptime_percent,
        msg->nmea_errors,
        msg->uart_errors,
        msg->rtcm_queue_overflows,
        msg->ntrip_timeouts
    );
}
