#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"

// WiFi AP configuration
#define WIFI_AP_SSID        "NTRIPClient"
#define WIFI_AP_PASSWORD    "config123"
#define WIFI_AP_CHANNEL     1
#define WIFI_AP_MAX_CONN    4
#define WIFI_AP_IP          "192.168.4.1"

/**
 * @brief WiFi connection status
 */
typedef struct {
    bool ap_enabled;
    bool sta_connected;
    char sta_ip[16];
    int8_t rssi;
} wifi_status_t;

/**
 * @brief Initialize WiFi Manager in AP+STA mode
 * 
 * Initializes WiFi in Access Point + Station mode simultaneously.
 * The AP is always active for configuration access at 192.168.4.1.
 * The STA mode connects to the user's router based on saved credentials.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Connect to WiFi network in Station mode
 * 
 * Attempts to connect to the configured WiFi network while keeping AP active.
 * 
 * @param ssid WiFi SSID to connect to
 * @param password WiFi password
 * @return ESP_OK if connection initiated, error code otherwise
 */
esp_err_t wifi_manager_connect_sta(const char* ssid, const char* password);

/**
 * @brief Disconnect from WiFi network in Station mode
 * 
 * Disconnects from the current WiFi network but keeps AP active.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_disconnect_sta(void);

/**
 * @brief Get WiFi connection status
 * 
 * Retrieves the current WiFi status including AP and STA states.
 * 
 * @param status Pointer to wifi_status_t structure to fill
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_manager_get_status(wifi_status_t* status);

/**
 * @brief Check if Station mode is connected
 * 
 * @return true if connected to WiFi network, false otherwise
 */
bool wifi_manager_is_sta_connected(void);

/**
 * @brief Get the Station mode IP address
 * 
 * @param ip_str Buffer to store IP address string (minimum 16 bytes)
 * @return ESP_OK if connected and IP obtained, error code otherwise
 */
esp_err_t wifi_manager_get_sta_ip(char* ip_str);

#endif // WIFI_MANAGER_H
