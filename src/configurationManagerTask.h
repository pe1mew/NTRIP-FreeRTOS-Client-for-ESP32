#ifndef CONFIGURATION_MANAGER_TASK_H
#define CONFIGURATION_MANAGER_TASK_H

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

// Configuration event bits for task notifications
#define CONFIG_WIFI_CHANGED_BIT     (1 << 0)
#define CONFIG_NTRIP_CHANGED_BIT    (1 << 1)
#define CONFIG_MQTT_CHANGED_BIT     (1 << 2)
#define CONFIG_ALL_CHANGED_BIT      (CONFIG_WIFI_CHANGED_BIT | CONFIG_NTRIP_CHANGED_BIT | CONFIG_MQTT_CHANGED_BIT)


// UI configuration structure
typedef struct {
    char password[64]; // UI password for web interface
} ui_config_t;

// WiFi configuration structure
typedef struct {
    char ssid[32];
    char password[64];
    char ap_password[64];
} app_wifi_config_t;

// NTRIP configuration structure
typedef struct {
    char host[128];
    uint16_t port;
    char mountpoint[64];
    char user[32];
    char password[64];
    uint16_t gga_interval_sec;     // Default: 120
    uint16_t reconnect_delay_sec;  // Default: 5
    bool enabled;                  // Default: true
} ntrip_config_t;

// MQTT configuration structure
typedef struct {
    char broker[128];
    uint16_t port;
    char topic[64];
    char user[32];
    char password[64];
    uint16_t gnss_interval_sec;    // Default: 10
    uint16_t status_interval_sec;  // Default: 120
    uint16_t stats_interval_sec;   // Default: 60
    bool enabled;                  // Default: true
} mqtt_config_t;

// Application configuration structure (combined)
typedef struct {
    ui_config_t ui;
    app_wifi_config_t wifi;
    ntrip_config_t ntrip;
    mqtt_config_t mqtt;
} app_config_t;

/**
 * @brief Initialize the Configuration Manager
 * 
 * This function initializes NVS storage, creates the configuration mutex,
 * creates the configuration event group, and loads configuration from NVS.
 * If no configuration exists, default values are used.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_manager_init(void);

/**
 * @brief Get WiFi configuration (thread-safe)
 * 
 * @param config Pointer to app_wifi_config_t structure to fill
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_get_wifi(app_wifi_config_t* config);

/**
 * @brief Get NTRIP configuration (thread-safe)
 * 
 * @param config Pointer to ntrip_config_t structure to fill
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_get_ntrip(ntrip_config_t* config);

/**
 * @brief Get MQTT configuration (thread-safe)
 * 
 * @param config Pointer to mqtt_config_t structure to fill
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_get_mqtt(mqtt_config_t* config);

/**
 * @brief Get MQTT configuration (thread-safe) - Compatibility wrapper
 * 
 * Same as config_get_mqtt, provided for naming consistency with other modules
 * 
 * @param config Pointer to mqtt_config_t structure to fill
 * @return ESP_OK on success, error code otherwise
 */
static inline esp_err_t config_manager_get_mqtt_config(mqtt_config_t* config) {
    return config_get_mqtt(config);
}

/**
 * @brief Get complete application configuration (thread-safe)
 * 
 * @param config Pointer to app_config_t structure to fill
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_get_all(app_config_t* config);

/**
 * @brief Set WiFi configuration (thread-safe)
 * 
 * Saves configuration to NVS and sets CONFIG_WIFI_CHANGED_BIT event
 * 
 * @param config Pointer to app_wifi_config_t structure with new values
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_set_wifi(const app_wifi_config_t* config);

/**
 * @brief Set NTRIP configuration (thread-safe)
 * 
 * Saves configuration to NVS and sets CONFIG_NTRIP_CHANGED_BIT event
 * 
 * @param config Pointer to ntrip_config_t structure with new values
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_set_ntrip(const ntrip_config_t* config);
esp_err_t config_set_ntrip_enabled_runtime(bool enabled);

/**
 * @brief Set MQTT configuration (thread-safe)
 * 
 * Saves configuration to NVS and sets CONFIG_MQTT_CHANGED_BIT event
 * 
 * @param config Pointer to mqtt_config_t structure with new values
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_set_mqtt(const mqtt_config_t* config);
esp_err_t config_set_mqtt_enabled_runtime(bool enabled);

/**
 * @brief Set complete application configuration (thread-safe)
 * 
 * Saves all configuration to NVS and sets CONFIG_ALL_CHANGED_BIT event
 * 
 * @param config Pointer to app_config_t structure with new values
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_set_all(const app_config_t* config);

/**
 * @brief Get the configuration event group handle
 * 
 * Tasks can wait on this event group to be notified of configuration changes
 * 
 * @return EventGroupHandle_t Configuration event group handle
 */
EventGroupHandle_t config_get_event_group(void);

/**
 * @brief Wait for configuration event bits with timeout
 * 
 * Helper function for tasks to wait for specific configuration changes.
 * Automatically clears the bits after reading.
 * 
 * @param bits_to_wait_for Event bits to wait for (CONFIG_WIFI_CHANGED_BIT, etc.)
 * @param timeout_ms Timeout in milliseconds (0 = no wait, portMAX_DELAY = wait forever)
 * @return EventBits_t The event bits that were set
 */
EventBits_t config_wait_for_event(EventBits_t bits_to_wait_for, TickType_t timeout_ms);

/**
 * @brief Perform factory reset (clear all configuration)
 * 
 * Erases all configuration from NVS and resets to default values
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_factory_reset(void);

/**
 * @brief Load default configuration values
 * 
 * Used for factory reset or first boot when no configuration exists in NVS
 * 
 * @param config Pointer to app_config_t structure to fill with defaults
 */
void config_load_defaults(app_config_t* config);

#endif // CONFIGURATION_MANAGER_TASK_H
