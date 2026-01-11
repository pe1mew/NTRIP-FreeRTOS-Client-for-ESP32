#include "wifiManager.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
void wifi_manager_get_ap_ssid(char* out_ssid, unsigned int len) {
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    snprintf(out_ssid, len, "%s-%02X%02X", WIFI_AP_SSID, mac[4], mac[5]);
}
#include "wifiManager.h"
#include "configurationManagerTask.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "lwip/inet.h"
#include <string.h>

static const char* TAG = "WiFiManager";

// WiFi connection state
static bool ap_enabled = false;
static bool sta_connected = false;
static bool sta_connecting = false;
static char sta_ip_address[16] = "0.0.0.0";
static int8_t sta_rssi = 0;

// Retry logic with exponential backoff
static int64_t first_disconnect_time = 0;  // Time of first disconnect (microseconds)
static int64_t last_retry_time = 0;        // Time of last retry attempt (microseconds)
static bool retry_active = false;           // Whether retry logic is active

// Retry intervals
#define FAST_RETRY_INTERVAL_MS   5000    // 5 seconds during first 30 seconds
#define SLOW_RETRY_INTERVAL_MS   60000   // 60 seconds after 30 seconds
#define FAST_RETRY_PERIOD_MS     30000   // 30 seconds of fast retry

// Event group for WiFi events
static EventGroupHandle_t wifi_event_group = NULL;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// WiFi netif handles
static esp_netif_t* netif_ap = NULL;
static esp_netif_t* netif_sta = NULL;

/**
 * @brief WiFi event handler
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
                ESP_LOGD(TAG, "Station " MACSTR " joined AP, AID=%d",
                         MAC2STR(event->mac), event->aid);
                break;
            }
            
        case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
                ESP_LOGD(TAG, "Station " MACSTR " left AP, AID=%d",
                         MAC2STR(event->mac), event->aid);
                break;
            }
            
            case WIFI_EVENT_STA_START:
                ESP_LOGD(TAG, "STA mode started");
                // Only connect if we have valid credentials
                wifi_config_t sta_config;
                if (esp_wifi_get_config(WIFI_IF_STA, &sta_config) == ESP_OK) {
                    if (strlen((char*)sta_config.sta.ssid) > 0) {
                        ESP_LOGD(TAG, "Attempting to connect to saved network...");
                        // Reset retry logic
                        retry_active = false;
                        first_disconnect_time = 0;
                        last_retry_time = 0;
                        esp_wifi_connect();
                    } else {
                        ESP_LOGI(TAG, "No WiFi credentials configured, STA idle");
                    }
                }
                break;
            
            case WIFI_EVENT_STA_CONNECTED: {
                wifi_event_sta_connected_t* event = (wifi_event_sta_connected_t*) event_data;
                ESP_LOGI(TAG, "Connected to AP SSID:%s", event->ssid);
                
                // Reset retry logic on successful connection
                retry_active = false;
                first_disconnect_time = 0;
                last_retry_time = 0;
                sta_connecting = false;
                
                // Get RSSI
                wifi_ap_record_t ap_info;
                if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                    sta_rssi = ap_info.rssi;
                }
                break;
            }
            
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
                int64_t now = esp_timer_get_time();
                
                sta_connected = false;
                sta_connecting = false;
                strcpy(sta_ip_address, "0.0.0.0");
                sta_rssi = 0;
                
                if (wifi_event_group) {
                    xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
                }
                
                // Initialize retry logic on first disconnect
                if (!retry_active) {
                    retry_active = true;
                    first_disconnect_time = now;
                    last_retry_time = now;
                    ESP_LOGI(TAG, "Disconnected from AP, reason: %d. Starting retry sequence...", event->reason);
                    sta_connecting = true;
                    esp_wifi_connect();
                } else {
                    // Calculate time since first disconnect
                    int64_t time_since_first_disconnect_ms = (now - first_disconnect_time) / 1000;
                    int64_t time_since_last_retry_ms = (now - last_retry_time) / 1000;
                    
                    // Determine retry interval based on elapsed time
                    int32_t retry_interval_ms;
                    if (time_since_first_disconnect_ms < FAST_RETRY_PERIOD_MS) {
                        retry_interval_ms = FAST_RETRY_INTERVAL_MS;
                        ESP_LOGD(TAG, "Disconnected (fast retry mode). Last retry %lld ms ago", time_since_last_retry_ms);
                    } else {
                        retry_interval_ms = SLOW_RETRY_INTERVAL_MS;
                        ESP_LOGD(TAG, "Disconnected (slow retry mode). Last retry %lld ms ago", time_since_last_retry_ms);
                    }
                    
                    // Only retry if enough time has passed since last attempt
                    if (time_since_last_retry_ms >= retry_interval_ms) {
                        last_retry_time = now;
                        ESP_LOGD(TAG, "Retrying connection... (elapsed: %lld s)", time_since_first_disconnect_ms / 1000);
                        sta_connecting = true;
                        esp_wifi_connect();
                    } else {
                        ESP_LOGD(TAG, "Waiting before retry (%lld/%d ms)", 
                                 time_since_last_retry_ms, retry_interval_ms);
                    }
                }
                break;
            }
            
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP: {
                ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                snprintf(sta_ip_address, sizeof(sta_ip_address), IPSTR, IP2STR(&event->ip_info.ip));
                ESP_LOGI(TAG, "Got IP address: %s", sta_ip_address);
                
                sta_connected = true;
                
                if (wifi_event_group) {
                    xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
                }
                break;
            }
            
            case IP_EVENT_STA_LOST_IP:
                ESP_LOGW(TAG, "Lost IP address");
                sta_connected = false;
                strcpy(sta_ip_address, "0.0.0.0");
                break;
            
            default:
                break;
        }
    }
}

/**
 * @brief Initialize WiFi Access Point
 */
static esp_err_t wifi_init_ap(void) {
    wifi_config_t wifi_config = {};
    app_wifi_config_t stored_config;
    config_get_wifi(&stored_config);
    // Create SSID with MAC address - use base SSID first, will update after start
    strncpy((char*)wifi_config.ap.ssid, WIFI_AP_SSID, sizeof(wifi_config.ap.ssid));
    if (strlen(stored_config.ap_password) > 0) {
        strncpy((char*)wifi_config.ap.password, stored_config.ap_password, sizeof(wifi_config.ap.password));
    } else {
        strncpy((char*)wifi_config.ap.password, WIFI_AP_PASSWORD, sizeof(wifi_config.ap.password));
    }
    wifi_config.ap.ssid_len = 0; // Let ESP-IDF calculate from null-terminated string
    wifi_config.ap.channel = WIFI_AP_CHANNEL;
    wifi_config.ap.max_connection = WIFI_AP_MAX_CONN;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    if (strlen((const char*)wifi_config.ap.password) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    ESP_LOGD(TAG, "Configuring AP with base SSID: %s", WIFI_AP_SSID);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    return ESP_OK;
}

/**
 * @brief Initialize WiFi Station mode with saved credentials
 */
static esp_err_t wifi_init_sta(void) {
    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));
    
    // Get WiFi credentials from configuration manager
    app_wifi_config_t stored_config;
    if (config_get_wifi(&stored_config) == ESP_OK) {
        if (strlen(stored_config.ssid) > 0) {
            strncpy((char*)wifi_config.sta.ssid, stored_config.ssid, sizeof(wifi_config.sta.ssid));
            strncpy((char*)wifi_config.sta.password, stored_config.password, sizeof(wifi_config.sta.password));
            
            ESP_LOGI(TAG, "WiFi STA configured with SSID: %s", stored_config.ssid);
        } else {
            ESP_LOGI(TAG, "No WiFi credentials configured, STA mode ready but not connecting");
        }
    } else {
        ESP_LOGW(TAG, "Failed to load WiFi configuration");
    }
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    return ESP_OK;
}

esp_err_t wifi_manager_init(void) {
    esp_err_t ret;
    
    // Create WiFi event group
    wifi_event_group = xEventGroupCreate();
    if (wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi event group");
        return ESP_FAIL;
    }
    
    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Create network interfaces
    netif_ap = esp_netif_create_default_wifi_ap();
    netif_sta = esp_netif_create_default_wifi_sta();
    
    if (netif_ap == NULL || netif_sta == NULL) {
        ESP_LOGE(TAG, "Failed to create network interfaces");
        return ESP_FAIL;
    }
    
    // Configure AP IP address (192.168.4.1)
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    
    esp_netif_dhcps_stop(netif_ap);
    esp_netif_set_ip_info(netif_ap, &ip_info);
    esp_netif_dhcps_start(netif_ap);
    
    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_LOST_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    
    // Set WiFi mode to AP+STA
    ESP_LOGD(TAG, "Setting WiFi mode to AP+STA...");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    
    // Set WiFi storage to RAM (config in memory only, we use NVS ourselves)
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    
    // Initialize AP
    ret = wifi_init_ap();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi AP");
        return ret;
    }
    
    // Initialize STA
    ret = wifi_init_sta();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi STA");
        return ret;
    }
    
    // Start WiFi
    ESP_LOGD(TAG, "Starting WiFi...");
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // Now update AP SSID with MAC address
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    
    char ssid_with_mac[32];
    snprintf(ssid_with_mac, sizeof(ssid_with_mac), "%s-%02X%02X", 
             WIFI_AP_SSID, mac[4], mac[5]);
    
    // Get current config and update SSID
    wifi_config_t wifi_config = {};
    ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_AP, &wifi_config));
    strncpy((char*)wifi_config.ap.ssid, ssid_with_mac, sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = 0; // Let ESP-IDF calculate
    
    // Stop WiFi, reconfigure, restart
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi Manager initialized in AP+STA mode");
    ESP_LOGI(TAG, "AP SSID: %s, Password: %s, IP: %s", 
             ssid_with_mac, WIFI_AP_PASSWORD, WIFI_AP_IP);
    
    // Set flag
    ap_enabled = true;
    
    return ESP_OK;
}

esp_err_t wifi_manager_connect_sta(const char* ssid, const char* password) {
    if (ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", ssid);
    
    // Disconnect first if already connected or connecting
    if (sta_connected || sta_connecting) {
        ESP_LOGI(TAG, "Disconnecting from current WiFi...");
        esp_wifi_disconnect();
        // Wait a bit for disconnect to complete
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    // Reset retry logic for new connection attempt
    retry_active = false;
    first_disconnect_time = 0;
    last_retry_time = 0;
    
    // Configure new credentials
    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    
    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi config: %s", esp_err_to_name(ret));
        return ret;
    }
    
    sta_connecting = true;
    return esp_wifi_connect();
}

esp_err_t wifi_manager_disconnect_sta(void) {
    if (sta_connected || sta_connecting) {
        ESP_LOGI(TAG, "Disconnecting from WiFi");
        sta_connected = false;
        sta_connecting = false;
        
        // Reset retry logic
        retry_active = false;
        first_disconnect_time = 0;
        last_retry_time = 0;
        
        strcpy(sta_ip_address, "0.0.0.0");
        return esp_wifi_disconnect();
    }
    return ESP_OK;
}

esp_err_t wifi_manager_get_status(wifi_status_t* status) {
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    status->ap_enabled = true;  // AP is always enabled
    status->sta_connected = sta_connected;
    strncpy(status->sta_ip, sta_ip_address, sizeof(status->sta_ip));
    status->rssi = sta_rssi;
    
    return ESP_OK;
}

bool wifi_manager_is_sta_connected(void) {
    return sta_connected;
}

esp_err_t wifi_manager_get_sta_ip(char* ip_str) {
    if (ip_str == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (sta_connected) {
        strncpy(ip_str, sta_ip_address, 16);
        return ESP_OK;
    }
    
    return ESP_FAIL;
}
