#include "configurationManagerTask.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char* TAG = "ConfigManager";

// NVS namespace names
#define NVS_NAMESPACE_WIFI   "wifi"
#define NVS_NAMESPACE_NTRIP  "ntrip"
#define NVS_NAMESPACE_MQTT   "mqtt"

// Configuration mutex for thread-safe access
static SemaphoreHandle_t config_mutex = NULL;

// Configuration event group for task notifications
static EventGroupHandle_t config_event_group = NULL;

// In-memory configuration cache
static app_config_t app_config;

// Default configuration values
static const app_config_t default_config = {
    .ui = {
        .password = "admin" // Default UI password
    },
    .wifi = {
        .ssid = "YourWiFiSSID",
        .password = "YourWiFiPassword",
        .ap_password = "config123"
    },
    .ntrip = {
        .host = "rtk2go.com",
        .port = 2101,
        .mountpoint = "YourMountpoint",
        .user = "user",
        .password = "password",
        .gga_interval_sec = 120,
        .reconnect_delay_sec = 5,
        .enabled = false  // Disabled by default until configured
    },
    .mqtt = {
        .broker = "mqtt.example.com",
        .port = 1883,
        .topic = "ntripclient",
        .user = "mqttuser",
        .password = "mqttpassword",
        .gnss_interval_sec = 10,
        .status_interval_sec = 120,
        .stats_interval_sec = 60,
        .enabled = false  // Disabled by default until configured
    }
};

/**
 * @brief Load UI configuration from NVS
 */
static esp_err_t nvs_load_ui(ui_config_t* config) {
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open("ui", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "UI config not found in NVS, using defaults");
        return err;
    }

    size_t size = sizeof(config->password);
    err = nvs_get_str(handle, "password", config->password, &size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read UI password from NVS");
    }
    nvs_close(handle);
    return ESP_OK;
}

/**
 * @brief Save UI configuration to NVS
 */
static esp_err_t nvs_save_ui(const ui_config_t* config) {
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open("ui", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for UI config: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(handle, "password", config->password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write UI password to NVS: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }
    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit UI config to NVS: %s", esp_err_to_name(err));
    }
    nvs_close(handle);
    ESP_LOGI(TAG, "UI config saved to NVS");
    return err;
}

/**
 * @brief Load WiFi configuration from NVS
 */
static esp_err_t nvs_load_wifi(app_wifi_config_t* config) {
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE_WIFI, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi config not found in NVS, using defaults");
        return err;
    }

    size_t size = sizeof(config->ssid);
    err = nvs_get_str(handle, "ssid", config->ssid, &size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read WiFi SSID from NVS");
    }

    size = sizeof(config->password);
    err = nvs_get_str(handle, "password", config->password, &size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read WiFi password from NVS");
    }

    size = sizeof(config->ap_password);
    err = nvs_get_str(handle, "ap_password", config->ap_password, &size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read AP password from NVS");
    }
    nvs_close(handle);
    return ESP_OK;
}

/**
 * @brief Save WiFi configuration to NVS
 */
static esp_err_t nvs_save_wifi(const app_wifi_config_t* config) {
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE_WIFI, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for WiFi config: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(handle, "ssid", config->ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write WiFi SSID to NVS: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_set_str(handle, "password", config->password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write WiFi password to NVS: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_set_str(handle, "ap_password", config->ap_password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write AP password to NVS: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }
    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit WiFi config to NVS: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "WiFi config saved to NVS");
    return err;
}

/**
 * @brief Load NTRIP configuration from NVS
 */
static esp_err_t nvs_load_ntrip(ntrip_config_t* config) {
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE_NTRIP, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NTRIP config not found in NVS, using defaults");
        return err;
    }

    size_t size = sizeof(config->host);
    nvs_get_str(handle, "host", config->host, &size);

    nvs_get_u16(handle, "port", &config->port);

    size = sizeof(config->mountpoint);
    nvs_get_str(handle, "mountpoint", config->mountpoint, &size);

    size = sizeof(config->user);
    nvs_get_str(handle, "user", config->user, &size);

    size = sizeof(config->password);
    nvs_get_str(handle, "password", config->password, &size);

    nvs_get_u16(handle, "gga_interval", &config->gga_interval_sec);
    nvs_get_u16(handle, "reconnect_delay", &config->reconnect_delay_sec);

    uint8_t enabled;
    if (nvs_get_u8(handle, "enabled", &enabled) == ESP_OK) {
        config->enabled = (enabled != 0);
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "NTRIP config loaded from NVS");
    return ESP_OK;
}

/**
 * @brief Save NTRIP configuration to NVS
 */
static esp_err_t nvs_save_ntrip(const ntrip_config_t* config) {
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE_NTRIP, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for NTRIP config: %s", esp_err_to_name(err));
        return err;
    }

    nvs_set_str(handle, "host", config->host);
    nvs_set_u16(handle, "port", config->port);
    nvs_set_str(handle, "mountpoint", config->mountpoint);
    nvs_set_str(handle, "user", config->user);
    nvs_set_str(handle, "password", config->password);
    nvs_set_u16(handle, "gga_interval", config->gga_interval_sec);
    nvs_set_u16(handle, "reconnect_delay", config->reconnect_delay_sec);
    nvs_set_u8(handle, "enabled", config->enabled ? 1 : 0);

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NTRIP config to NVS: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "NTRIP config saved to NVS");
    return err;
}

/**
 * @brief Load MQTT configuration from NVS
 */
static esp_err_t nvs_load_mqtt(mqtt_config_t* config) {
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE_MQTT, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MQTT config not found in NVS, using defaults");
        return err;
    }

    size_t size = sizeof(config->broker);
    nvs_get_str(handle, "broker", config->broker, &size);

    nvs_get_u16(handle, "port", &config->port);

    size = sizeof(config->topic);
    nvs_get_str(handle, "topic", config->topic, &size);

    size = sizeof(config->user);
    nvs_get_str(handle, "user", config->user, &size);

    size = sizeof(config->password);
    nvs_get_str(handle, "password", config->password, &size);

    nvs_get_u16(handle, "gnss_interval", &config->gnss_interval_sec);
    nvs_get_u16(handle, "status_interval", &config->status_interval_sec);
    nvs_get_u16(handle, "stats_interval", &config->stats_interval_sec);

    uint8_t enabled;
    if (nvs_get_u8(handle, "enabled", &enabled) == ESP_OK) {
        config->enabled = (enabled != 0);
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "MQTT config loaded from NVS");
    return ESP_OK;
}

/**
 * @brief Save MQTT configuration to NVS
 */
static esp_err_t nvs_save_mqtt(const mqtt_config_t* config) {
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE_MQTT, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for MQTT config: %s", esp_err_to_name(err));
        return err;
    }

    nvs_set_str(handle, "broker", config->broker);
    nvs_set_u16(handle, "port", config->port);
    nvs_set_str(handle, "topic", config->topic);
    nvs_set_str(handle, "user", config->user);
    nvs_set_str(handle, "password", config->password);
    nvs_set_u16(handle, "gnss_interval", config->gnss_interval_sec);
    nvs_set_u16(handle, "status_interval", config->status_interval_sec);
    nvs_set_u16(handle, "stats_interval", config->stats_interval_sec);
    nvs_set_u8(handle, "enabled", config->enabled ? 1 : 0);

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit MQTT config to NVS: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "MQTT config saved to NVS");
    return err;
}

void config_load_defaults(app_config_t* config) {
    memcpy(config, &default_config, sizeof(app_config_t));
    ESP_LOGI(TAG, "Loaded default configuration");
}

esp_err_t config_manager_init(void) {
    esp_err_t err;

    // Initialize NVS
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        ESP_LOGW(TAG, "NVS partition needs erasing, performing erase...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "NVS initialized successfully");

    // Create configuration mutex
    config_mutex = xSemaphoreCreateMutex();
    if (config_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create configuration mutex");
        return ESP_FAIL;
    }

    // Create configuration event group
    config_event_group = xEventGroupCreate();
    if (config_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create configuration event group");
        return ESP_FAIL;
    }

    // Load default configuration first

    config_load_defaults(&app_config);

    // Try to load configuration from NVS, keep defaults if not found
    nvs_load_ui(&app_config.ui);
    nvs_load_wifi(&app_config.wifi);
    nvs_load_ntrip(&app_config.ntrip);
    nvs_load_mqtt(&app_config.mqtt);

    ESP_LOGI(TAG, "Configuration Manager initialized");
    ESP_LOGI(TAG, "  WiFi SSID: %s", app_config.wifi.ssid);
    ESP_LOGI(TAG, "  NTRIP Host: %s:%d", app_config.ntrip.host, app_config.ntrip.port);
    ESP_LOGI(TAG, "  NTRIP Enabled: %s", app_config.ntrip.enabled ? "Yes" : "No");
    ESP_LOGI(TAG, "  MQTT Broker: %s:%d", app_config.mqtt.broker, app_config.mqtt.port);
    ESP_LOGI(TAG, "  MQTT Enabled: %s", app_config.mqtt.enabled ? "Yes" : "No");

    return ESP_OK;
}

esp_err_t config_get_wifi(app_wifi_config_t* config) {
    if (config == NULL || config_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(config_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        memcpy(config, &app_config.wifi, sizeof(app_wifi_config_t));
        xSemaphoreGive(config_mutex);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to acquire mutex for WiFi config read");
    return ESP_ERR_TIMEOUT;
}

esp_err_t config_get_ntrip(ntrip_config_t* config) {
    if (config == NULL || config_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(config_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        memcpy(config, &app_config.ntrip, sizeof(ntrip_config_t));
        xSemaphoreGive(config_mutex);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to acquire mutex for NTRIP config read");
    return ESP_ERR_TIMEOUT;
}

esp_err_t config_get_mqtt(mqtt_config_t* config) {
    if (config == NULL || config_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(config_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        memcpy(config, &app_config.mqtt, sizeof(mqtt_config_t));
        xSemaphoreGive(config_mutex);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to acquire mutex for MQTT config read");
    return ESP_ERR_TIMEOUT;
}

esp_err_t config_get_all(app_config_t* config) {
    if (config == NULL || config_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(config_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        memcpy(config, &app_config, sizeof(app_config_t));
        xSemaphoreGive(config_mutex);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to acquire mutex for complete config read");
    return ESP_ERR_TIMEOUT;
}

esp_err_t config_set_wifi(const app_wifi_config_t* config) {
    if (config == NULL || config_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ESP_OK;

    if (xSemaphoreTake(config_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // Update in-memory configuration
        memcpy(&app_config.wifi, config, sizeof(app_wifi_config_t));
        
        // Save to NVS
        err = nvs_save_wifi(config);
        
        xSemaphoreGive(config_mutex);

        // Notify tasks of configuration change
        if (config_event_group != NULL) {
            xEventGroupSetBits(config_event_group, CONFIG_WIFI_CHANGED_BIT);
        }

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "WiFi configuration updated");
        }
        return err;
    }

    ESP_LOGE(TAG, "Failed to acquire mutex for WiFi config write");
    return ESP_ERR_TIMEOUT;
}

esp_err_t config_set_ntrip(const ntrip_config_t* config) {
    if (config == NULL || config_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ESP_OK;

    if (xSemaphoreTake(config_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // Update in-memory configuration
        memcpy(&app_config.ntrip, config, sizeof(ntrip_config_t));
        
        // Save to NVS
        err = nvs_save_ntrip(config);
        
        xSemaphoreGive(config_mutex);

        // Notify tasks of configuration change
        if (config_event_group != NULL) {
            xEventGroupSetBits(config_event_group, CONFIG_NTRIP_CHANGED_BIT);
        }

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "NTRIP configuration updated (enabled: %s)", 
                     config->enabled ? "Yes" : "No");
        }
        return err;
    }

    ESP_LOGE(TAG, "Failed to acquire mutex for NTRIP config write");
    return ESP_ERR_TIMEOUT;
}

esp_err_t config_set_ntrip_enabled_runtime(bool enabled) {
    if (config_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(config_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        app_config.ntrip.enabled = enabled;
        xSemaphoreGive(config_mutex);

        if (config_event_group != NULL) {
            xEventGroupSetBits(config_event_group, CONFIG_NTRIP_CHANGED_BIT);
        }

        ESP_LOGI(TAG, "NTRIP runtime enabled set to %s (no NVS write)", enabled ? "true" : "false");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to acquire mutex for NTRIP runtime enable");
    return ESP_ERR_TIMEOUT;
}

esp_err_t config_set_mqtt(const mqtt_config_t* config) {
    if (config == NULL || config_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ESP_OK;

    if (xSemaphoreTake(config_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // Update in-memory configuration
        memcpy(&app_config.mqtt, config, sizeof(mqtt_config_t));
        
        // Save to NVS
        err = nvs_save_mqtt(config);
        
        xSemaphoreGive(config_mutex);

        // Notify tasks of configuration change
        if (config_event_group != NULL) {
            xEventGroupSetBits(config_event_group, CONFIG_MQTT_CHANGED_BIT);
        }

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "MQTT configuration updated (enabled: %s)", 
                     config->enabled ? "Yes" : "No");
        }
        return err;
    }

    ESP_LOGE(TAG, "Failed to acquire mutex for MQTT config write");
    return ESP_ERR_TIMEOUT;
}

esp_err_t config_set_mqtt_enabled_runtime(bool enabled) {
    if (config_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(config_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        app_config.mqtt.enabled = enabled;
        xSemaphoreGive(config_mutex);

        if (config_event_group != NULL) {
            xEventGroupSetBits(config_event_group, CONFIG_MQTT_CHANGED_BIT);
        }

        ESP_LOGI(TAG, "MQTT runtime enabled set to %s (no NVS write)", enabled ? "true" : "false");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to acquire mutex for MQTT runtime enable");
    return ESP_ERR_TIMEOUT;
}

esp_err_t config_set_all(const app_config_t* config) {
    if (config == NULL || config_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ESP_OK;

    if (xSemaphoreTake(config_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // Update in-memory configuration
        memcpy(&app_config, config, sizeof(app_config_t));
        
        // Save all to NVS
        esp_err_t err_ui = nvs_save_ui(&config->ui);
        esp_err_t err_wifi = nvs_save_wifi(&config->wifi);
        esp_err_t err_ntrip = nvs_save_ntrip(&config->ntrip);
        esp_err_t err_mqtt = nvs_save_mqtt(&config->mqtt);
        
        // Return first error encountered
        if (err_ui != ESP_OK) err = err_ui;
        else if (err_wifi != ESP_OK) err = err_wifi;
        else if (err_ntrip != ESP_OK) err = err_ntrip;
        else if (err_mqtt != ESP_OK) err = err_mqtt;
        
        xSemaphoreGive(config_mutex);

        // Notify tasks of configuration change
        if (config_event_group != NULL) {
            xEventGroupSetBits(config_event_group, CONFIG_ALL_CHANGED_BIT);
        }

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Complete configuration updated");
        }
        return err;
    }

    ESP_LOGE(TAG, "Failed to acquire mutex for complete config write");
    return ESP_ERR_TIMEOUT;
}

EventGroupHandle_t config_get_event_group(void) {
    return config_event_group;
}

esp_err_t config_factory_reset(void) {
    esp_err_t err;

    ESP_LOGW(TAG, "Performing factory reset...");

    // Erase all NVS namespaces
    nvs_handle_t handle;
    
    err = nvs_open(NVS_NAMESPACE_WIFI, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }

    err = nvs_open(NVS_NAMESPACE_NTRIP, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }

    err = nvs_open(NVS_NAMESPACE_MQTT, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }

    // Load defaults into memory
    if (config_mutex != NULL && xSemaphoreTake(config_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        config_load_defaults(&app_config);
        xSemaphoreGive(config_mutex);
    }

    // Notify all tasks of configuration reset
    if (config_event_group != NULL) {
        xEventGroupSetBits(config_event_group, CONFIG_ALL_CHANGED_BIT);
    }

    ESP_LOGI(TAG, "Factory reset complete, configuration restored to defaults");
    return ESP_OK;
}

EventBits_t config_wait_for_event(EventBits_t bits_to_wait_for, TickType_t timeout_ms) {
    if (config_event_group == NULL) {
        return 0;
    }
    
    // Convert milliseconds to ticks
    TickType_t timeout_ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    
    // Wait for the specified bits and clear them automatically
    return xEventGroupWaitBits(
        config_event_group,
        bits_to_wait_for,
        pdTRUE,  // Clear bits on exit
        pdFALSE, // Wait for ANY bit (not all)
        timeout_ticks
    );
}

// Expose the default UI password for web UI warning
const char* config_get_default_ui_password(void) {
    return default_config.ui.password;
}

/**
 * @brief Test if provided UI password matches the one stored in NVS
 */
bool config_test_ui_password(const char* password) {
    if (!password) return false;
    ui_config_t ui_cfg = {0};
    if (nvs_load_ui(&ui_cfg) != ESP_OK) {
        // If not found in NVS, fall back to default
        return strcmp(password, default_config.ui.password) == 0;
    }
    return strcmp(password, ui_cfg.password) == 0;
}