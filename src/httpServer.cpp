#include "httpServer.h"
#include "configurationManagerTask.h"
#include "ntripClientTask.h"
#include "mqttClientTask.h"
#include "wifiManager.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <string.h>

static const char* TAG = "HTTPServer";

static httpd_handle_t server = NULL;

// HTML content for main configuration page
static const char* html_page = 
"<!DOCTYPE html>\n"
"<html>\n"
"<head>\n"
"    <meta charset='utf-8'>\n"
"    <meta name='viewport' content='width=device-width, initial-scale=1'>\n"
"    <title>NTRIP-client Configuration</title>\n"
"    <style>\n"
"        body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }\n"
"        .container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 5px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }\n"
"        h1 { color: #333; border-bottom: 2px solid #0066cc; padding-bottom: 10px; }\n"
"        h2 { color: #0066cc; margin-top: 30px; }\n"
"        .form-group { margin-bottom: 15px; }\n"
"        label { display: block; margin-bottom: 5px; font-weight: bold; color: #555; }\n"
"        input[type='text'], input[type='password'], input[type='number'] { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }\n"
"        button { background: #0066cc; color: white; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; margin-right: 10px; }\n"
"        button:hover { background: #0052a3; }\n"
"        .status { padding: 10px; margin: 10px 0; border-radius: 4px; }\n"
"        .success { background: #d4edda; color: #155724; border: 1px solid #c3e6cb; }\n"
"        .error { background: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }\n"
"        .info { background: #d1ecf1; color: #0c5460; border: 1px solid #bee5eb; margin-top: 20px; }\n"
"        .switch-container { display: flex; align-items: center; margin-bottom: 15px; flex-wrap: nowrap; }\n"
"        .switch-container label { margin-bottom: 0; margin-right: 15px; }\n"
"        @media (max-width: 600px) {\n"
"            .switch-container { flex-direction: row; flex-wrap: nowrap; align-items: center; }\n"
"            .switch, .status-indicator, .switch-container label, .switch-container span { margin-left: 0; margin-right: 10px; }\n"
"            .status-indicator { margin-top: 0; }\n"
"        }\n"
"        .switch { position: relative; display: inline-block; width: 60px; height: 34px; }\n"
"        .switch input { opacity: 0; width: 0; height: 0; }\n"
"        .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; transition: background 0.4s; border-radius: 34px; }\n"
"        .slider:before { position: absolute; content: ''; height: 26px; width: 26px; left: 4px; bottom: 4px; background-color: white; transition: transform 0.4s; border-radius: 50%; box-shadow: 0 1px 3px rgba(0,0,0,0.08); }\n"
"        input:checked + .slider { background-color: #0066cc; }\n"
"        input:checked + .slider:before { transform: translateX(22px); } /* reduced to 22px to keep ball inside on Android Chrome */\n"
"        .status-indicator { width: 34px; height: 34px; border-radius: 50%; display: inline-block; margin-left: 12px; border: 2px solid #bbb; box-sizing: border-box; vertical-align: middle; transition: background 0.3s; }\n"
"        .status-indicator.connected { background: #28c940; border-color: #28c940; }\n"
"        .status-indicator.disconnected { background: #e74c3c; border-color: #e74c3c; }\n"
"    </style>\n"
"</head>\n"
"<body>\n"
"    <div class='container'>\n"
"        <h1>NTRIP-client Configuration</h1>\n"
"        <div id='status'></div>\n"
"        <div class='info'>\n"
"            <strong>System Status:</strong>\n"
"            <p>WiFi AP: 192.168.4.1 | STA: <span id='sta_status'>Checking...</span></p>\n"
"        </div>\n"
"        <h2>WiFi Configuration</h2>\n"
"        <div class='form-group'>\n"
"            <label>SSID:</label>\n"
"            <input type='text' id='wifi_ssid' maxlength='31'>\n"
"        </div>\n"
"        <div class='form-group'>\n"
"            <label>Password:</label>\n"
"            <input type='password' id='wifi_password' maxlength='63' placeholder='Leave blank to keep current password'>\n"
"        </div>\n"
"        <h2 style='margin-bottom: 8px;'>NTRIP Configuration</h2>\n"
"        <div class='switch-container'>\n"
"            <label>Enable NTRIP Client:</label>\n"
"            <label class='switch'>\n"
"                <input type='checkbox' id='ntrip_enabled' onchange='toggleService(\"ntrip\", this.checked)'>\n"
"                <span class='slider'></span>\n"
"            </label>\n"
"        </div>\n"
"        <div class='status-indicator-container' style='display: flex; align-items: center; margin-bottom: 15px;'>\n"
"            <span style='color:#555; font-size:15px; font-weight:bold; margin-right:6px;'>Connection status:</span>\n"
"            <span id='ntrip_status_indicator' class='status-indicator disconnected' title='NTRIP status'></span>\n"
"        </div>\n"
"        <div class='form-group'>\n"
"            <label>Host:</label>\n"
"            <input type='text' id='ntrip_host' maxlength='127'>\n"
"        </div>\n"
"        <div class='form-group'>\n"
"            <label>Port:</label>\n"
"            <input type='number' id='ntrip_port' min='1' max='65535'>\n"
"        </div>\n"
"        <div class='form-group'>\n"
"            <label>Mountpoint:</label>\n"
"            <input type='text' id='ntrip_mountpoint' maxlength='63'>\n"
"        </div>\n"
"        <div class='form-group'>\n"
"            <label>Username:</label>\n"
"            <input type='text' id='ntrip_user' maxlength='31'>\n"
"        </div>\n"
"        <div class='form-group'>\n"
"            <label>Password:</label>\n"
"            <input type='password' id='ntrip_password' maxlength='63' placeholder='Leave blank to keep current password'>\n"
"        </div>\n"
"        <div class='form-group'>\n"
"            <label>GGA Interval (seconds):</label>\n"
"            <input type='number' id='ntrip_gga_interval' min='10' max='600' value='120'>\n"
"        </div>\n"
"        <h2 style='margin-bottom: 8px;'>MQTT Configuration</h2>\n"
"        <div class='switch-container'>\n"
"            <label>Enable MQTT Client:</label>\n"
"            <label class='switch'>\n"
"                <input type='checkbox' id='mqtt_enabled' onchange='toggleService(\"mqtt\", this.checked)'>\n"
"                <span class='slider'></span>\n"
"            </label>\n"
"        </div>\n"
"        <div class='status-indicator-container' style='display: flex; align-items: center; margin-bottom: 15px;'>\n"
"            <span style='color:#555; font-size:15px; font-weight:bold; margin-right:6px;'>Connection status:</span>\n"
"            <span id='mqtt_status_indicator' class='status-indicator disconnected' title='MQTT status'></span>\n"
"        </div>\n"
"        <div class='form-group'>\n"
"            <label>Broker:</label>\n"
"            <input type='text' id='mqtt_broker' maxlength='127'>\n"
"        </div>\n"
"        <div class='form-group'>\n"
"            <label>Port:</label>\n"
"            <input type='number' id='mqtt_port' min='1' max='65535' value='1883'>\n"
"        </div>\n"
"        <div class='form-group'>\n"
"            <label>Topic:</label>\n"
"            <input type='text' id='mqtt_topic' maxlength='63'>\n"
"        </div>\n"
"        <div class='form-group'>\n"
"            <label>Username:</label>\n"
"            <input type='text' id='mqtt_user' maxlength='31'>\n"
"        </div>\n"
"        <div class='form-group'>\n"
"            <label>Password:</label>\n"
"            <input type='password' id='mqtt_password' maxlength='63' placeholder='Leave blank to keep current password'>\n"
"        </div>\n"
"        <div class='form-group'>\n"
"            <label>GNSS Interval (sec, 0=disabled):</label>\n"
"            <input type='number' id='mqtt_gnss_interval' min='0' max='300' value='10'>\n"
"        </div>\n"
"        <div class='form-group'>\n"
"            <label>Status Interval (sec, 0=disabled):</label>\n"
"            <input type='number' id='mqtt_status_interval' min='0' max='600' value='120'>\n"
"        </div>\n"
"        <div class='form-group'>\n"
"            <label>Stats Interval (sec, 0=disabled):</label>\n"
"            <input type='number' id='mqtt_stats_interval' min='0' max='600' value='60'>\n"
"        </div>\n"
"        <div style='margin-top: 30px;'>\n"
"            <button onclick='saveConfig()'>Save Configuration</button>\n"
"            <button onclick='restartDevice()'>Restart Device</button>\n"
"            <button onclick='factoryReset()' style='background: #dc3545;'>Factory Reset</button>\n"
"        </div>\n"
"    </div>\n"
"    <script>\n"
"        function loadConfig() {\n"
"            fetch('/api/config').then(r => r.json()).then(data => {\n"
"                document.getElementById('wifi_ssid').value = data.wifi.ssid;\n"
"                // Don't populate password fields - leave empty with placeholder\n"
"                document.getElementById('ntrip_enabled').checked = data.ntrip.enabled;\n"
"                document.getElementById('ntrip_host').value = data.ntrip.host;\n"
"                document.getElementById('ntrip_port').value = data.ntrip.port;\n"
"                document.getElementById('ntrip_mountpoint').value = data.ntrip.mountpoint;\n"
"                document.getElementById('ntrip_user').value = data.ntrip.user;\n"
"                document.getElementById('ntrip_gga_interval').value = data.ntrip.gga_interval_sec;\n"
"                document.getElementById('mqtt_enabled').checked = data.mqtt.enabled;\n"
"                document.getElementById('mqtt_broker').value = data.mqtt.broker;\n"
"                document.getElementById('mqtt_port').value = data.mqtt.port;\n"
"                document.getElementById('mqtt_topic').value = data.mqtt.topic;\n"
"                document.getElementById('mqtt_user').value = data.mqtt.user;\n"
"                document.getElementById('mqtt_gnss_interval').value = data.mqtt.gnss_interval_sec;\n"
"                document.getElementById('mqtt_status_interval').value = data.mqtt.status_interval_sec;\n"
"                document.getElementById('mqtt_stats_interval').value = data.mqtt.stats_interval_sec;\n"
"            }).catch(e => showStatus('Failed to load configuration', 'error'));\n"
"        }\n"
"        function saveConfig() {\n"
"            const config = {\n"
"                wifi: { ssid: document.getElementById('wifi_ssid').value, password: document.getElementById('wifi_password').value },\n"
"                ntrip: { enabled: document.getElementById('ntrip_enabled').checked, host: document.getElementById('ntrip_host').value,\n"
"                         port: parseInt(document.getElementById('ntrip_port').value), mountpoint: document.getElementById('ntrip_mountpoint').value,\n"
"                         user: document.getElementById('ntrip_user').value, password: document.getElementById('ntrip_password').value,\n"
"                         gga_interval_sec: parseInt(document.getElementById('ntrip_gga_interval').value), reconnect_delay_sec: 5 },\n"
"                mqtt: { enabled: document.getElementById('mqtt_enabled').checked, broker: document.getElementById('mqtt_broker').value,\n"
"                        port: parseInt(document.getElementById('mqtt_port').value), topic: document.getElementById('mqtt_topic').value,\n"
"                        user: document.getElementById('mqtt_user').value, password: document.getElementById('mqtt_password').value,\n"
"                        gnss_interval_sec: parseInt(document.getElementById('mqtt_gnss_interval').value),\n"
"                        status_interval_sec: parseInt(document.getElementById('mqtt_status_interval').value),\n"
"                        stats_interval_sec: parseInt(document.getElementById('mqtt_stats_interval').value) }\n"
"            };\n"
"            fetch('/api/config', { method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(config) })\n"
"            .then(r => r.json()).then(data => showStatus(data.message, data.status === 'ok' ? 'success' : 'error'))\n"
"            .catch(e => showStatus('Failed to save configuration', 'error'));\n"
"        }\n"
"        function restartDevice() {\n"
"            if(confirm('Restart device?')) {\n"
"                fetch('/api/restart', {method: 'POST'}).then(() => showStatus('Device restarting...', 'success'));\n"
"            }\n"
"        }\n"
"        function factoryReset() {\n"
"            if(confirm('Reset all settings to factory defaults?')) {\n"
"                fetch('/api/factory_reset', {method: 'POST'}).then(() => showStatus('Factory reset complete, device restarting...', 'success'));\n"
"            }\n"
"        }\n"
"        function showStatus(msg, type) {\n"
"            const div = document.getElementById('status');\n"
"            div.className = 'status ' + type;\n"
"            div.textContent = msg;\n"
"            setTimeout(() => div.textContent = '', 5000);\n"
"        }\n"
"        function toggleService(service, enabled) {\n"
"            const payload = { service: service, enabled: enabled };\n"
"            fetch('/api/toggle', { method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(payload) })\n"
"            .then(r => r.json()).then(data => {\n"
"                if (data.status === 'ok') {\n"
"                    showStatus(service.toUpperCase() + ' ' + (enabled ? 'enabled' : 'disabled'), 'success');\n"
"                } else {\n"
"                    showStatus(data.message || 'Failed to toggle ' + service, 'error');\n"
"                    document.getElementById(service + '_enabled').checked = !enabled;\n"
"                }\n"
"            }).catch(e => {\n"
"                showStatus('Failed to toggle ' + service, 'error');\n"
"                document.getElementById(service + '_enabled').checked = !enabled;\n"
"            });\n"
"        }\n"
"        function updateStatus() {\n"
"            fetch('/api/status').then(r => r.json()).then(data => {\n"
"                document.getElementById('sta_status').textContent = data.wifi.sta_connected ? data.wifi.sta_ip : 'Not connected';\n"
"                // NTRIP indicator\n"
"                var ntrip = data.ntrip_connected !== undefined ? data.ntrip_connected : false;\n"
"                var ntripInd = document.getElementById('ntrip_status_indicator');\n"
"                if (ntrip) {\n"
"                    ntripInd.classList.add('connected');\n"
"                    ntripInd.classList.remove('disconnected');\n"
"                } else {\n"
"                    ntripInd.classList.remove('connected');\n"
"                    ntripInd.classList.add('disconnected');\n"
"                }\n"
"                // MQTT indicator\n"
"                var mqtt = data.mqtt_connected !== undefined ? data.mqtt_connected : false;\n"
"                var mqttInd = document.getElementById('mqtt_status_indicator');\n"
"                if (mqtt) {\n"
"                    mqttInd.classList.add('connected');\n"
"                    mqttInd.classList.remove('disconnected');\n"
"                } else {\n"
"                    mqttInd.classList.remove('connected');\n"
"                    mqttInd.classList.add('disconnected');\n"
"                }\n"
"            });\n"
"        }\n"
"        loadConfig();\n"
"        setInterval(updateStatus, 5000);\n"
"        updateStatus();\n"
"    </script>\n"
"</body>\n"
"</html>";

/**
 * @brief Handler for root endpoint (/)
 */
static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_page, strlen(html_page));
    return ESP_OK;
}

/**
 * @brief Handler for GET /api/config
 */
static esp_err_t api_config_get_handler(httpd_req_t *req) {
    app_config_t config;
    
    if (config_get_all(&config) != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Failed to read configuration\"}");
        return ESP_FAIL;
    }
    
    // Create JSON response (mask passwords)
    cJSON *root = cJSON_CreateObject();
    
    // WiFi config
    cJSON *wifi = cJSON_CreateObject();
    cJSON_AddStringToObject(wifi, "ssid", config.wifi.ssid);
    cJSON_AddStringToObject(wifi, "password", "********");
    cJSON_AddItemToObject(root, "wifi", wifi);
    
    // NTRIP config
    cJSON *ntrip = cJSON_CreateObject();
    cJSON_AddStringToObject(ntrip, "host", config.ntrip.host);
    cJSON_AddNumberToObject(ntrip, "port", config.ntrip.port);
    cJSON_AddStringToObject(ntrip, "mountpoint", config.ntrip.mountpoint);
    cJSON_AddStringToObject(ntrip, "user", config.ntrip.user);
    cJSON_AddStringToObject(ntrip, "password", "********");
    cJSON_AddNumberToObject(ntrip, "gga_interval_sec", config.ntrip.gga_interval_sec);
    cJSON_AddNumberToObject(ntrip, "reconnect_delay_sec", config.ntrip.reconnect_delay_sec);
    cJSON_AddBoolToObject(ntrip, "enabled", config.ntrip.enabled);
    cJSON_AddItemToObject(root, "ntrip", ntrip);
    
    // MQTT config
    cJSON *mqtt = cJSON_CreateObject();
    cJSON_AddStringToObject(mqtt, "broker", config.mqtt.broker);
    cJSON_AddNumberToObject(mqtt, "port", config.mqtt.port);
    cJSON_AddStringToObject(mqtt, "topic", config.mqtt.topic);
    cJSON_AddStringToObject(mqtt, "user", config.mqtt.user);
    cJSON_AddStringToObject(mqtt, "password", "********");
    cJSON_AddNumberToObject(mqtt, "gnss_interval_sec", config.mqtt.gnss_interval_sec);
    cJSON_AddNumberToObject(mqtt, "status_interval_sec", config.mqtt.status_interval_sec);
    cJSON_AddNumberToObject(mqtt, "stats_interval_sec", config.mqtt.stats_interval_sec);
    cJSON_AddBoolToObject(mqtt, "enabled", config.mqtt.enabled);
    cJSON_AddItemToObject(root, "mqtt", mqtt);
    
    char *json_string = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_string);
    
    free(json_string);
    cJSON_Delete(root);
    
    return ESP_OK;
}

/**
 * @brief Handler for POST /api/config
 */
static esp_err_t api_config_post_handler(httpd_req_t *req) {
    char content[2048];
    int ret, remaining = req->content_len;
    
    if (remaining >= sizeof(content)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Request too large\"}");
        return ESP_FAIL;
    }
    
    ret = httpd_req_recv(req, content, remaining);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    // Parse JSON
    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return ESP_FAIL;
    }
    
    app_config_t config;
    app_config_t old_config;
    config_get_all(&config);  // Get current config first
    memcpy(&old_config, &config, sizeof(app_config_t));  // Keep a copy for comparison
    
    bool wifi_changed = false;
    bool ntrip_changed = false;
    bool mqtt_changed = false;
    
    // Parse WiFi config
    cJSON *wifi = cJSON_GetObjectItem(root, "wifi");
    if (wifi) {
        cJSON *ssid = cJSON_GetObjectItem(wifi, "ssid");
        cJSON *password = cJSON_GetObjectItem(wifi, "password");
        if (ssid && cJSON_IsString(ssid) && strcmp(config.wifi.ssid, ssid->valuestring) != 0) {
            strncpy(config.wifi.ssid, ssid->valuestring, sizeof(config.wifi.ssid) - 1);
            wifi_changed = true;
        }
        // Only update password if it's not empty AND different
        if (password && cJSON_IsString(password) && strlen(password->valuestring) > 0) {
            if (strcmp(config.wifi.password, password->valuestring) != 0) {
                strncpy(config.wifi.password, password->valuestring, sizeof(config.wifi.password) - 1);
                wifi_changed = true;
            }
        }
    }
    
    // Parse NTRIP config
    cJSON *ntrip = cJSON_GetObjectItem(root, "ntrip");
    if (ntrip) {
        cJSON *enabled = cJSON_GetObjectItem(ntrip, "enabled");
        cJSON *host = cJSON_GetObjectItem(ntrip, "host");
        cJSON *port = cJSON_GetObjectItem(ntrip, "port");
        cJSON *mountpoint = cJSON_GetObjectItem(ntrip, "mountpoint");
        cJSON *user = cJSON_GetObjectItem(ntrip, "user");
        cJSON *password = cJSON_GetObjectItem(ntrip, "password");
        cJSON *gga_interval = cJSON_GetObjectItem(ntrip, "gga_interval_sec");
        
        if (enabled && cJSON_IsBool(enabled)) { config.ntrip.enabled = cJSON_IsTrue(enabled); ntrip_changed = true; }
        if (host && cJSON_IsString(host)) { strncpy(config.ntrip.host, host->valuestring, sizeof(config.ntrip.host) - 1); ntrip_changed = true; }
        if (port && cJSON_IsNumber(port)) { config.ntrip.port = port->valueint; ntrip_changed = true; }
        if (mountpoint && cJSON_IsString(mountpoint)) { strncpy(config.ntrip.mountpoint, mountpoint->valuestring, sizeof(config.ntrip.mountpoint) - 1); ntrip_changed = true; }
        if (user && cJSON_IsString(user)) { strncpy(config.ntrip.user, user->valuestring, sizeof(config.ntrip.user) - 1); ntrip_changed = true; }
        // Only update password if it's not empty
        if (password && cJSON_IsString(password) && strlen(password->valuestring) > 0) {
            strncpy(config.ntrip.password, password->valuestring, sizeof(config.ntrip.password) - 1);
            ntrip_changed = true;
        }
        if (gga_interval && cJSON_IsNumber(gga_interval)) { config.ntrip.gga_interval_sec = gga_interval->valueint; ntrip_changed = true; }
    }
    
    // Parse MQTT config
    cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");
    if (mqtt) {
        cJSON *enabled = cJSON_GetObjectItem(mqtt, "enabled");
        cJSON *broker = cJSON_GetObjectItem(mqtt, "broker");
        cJSON *port = cJSON_GetObjectItem(mqtt, "port");
        cJSON *topic = cJSON_GetObjectItem(mqtt, "topic");
        cJSON *user = cJSON_GetObjectItem(mqtt, "user");
        cJSON *password = cJSON_GetObjectItem(mqtt, "password");
        cJSON *gnss_interval = cJSON_GetObjectItem(mqtt, "gnss_interval_sec");
        cJSON *status_interval = cJSON_GetObjectItem(mqtt, "status_interval_sec");
        cJSON *stats_interval = cJSON_GetObjectItem(mqtt, "stats_interval_sec");
        
        if (enabled && cJSON_IsBool(enabled)) { config.mqtt.enabled = cJSON_IsTrue(enabled); mqtt_changed = true; }
        if (broker && cJSON_IsString(broker)) { strncpy(config.mqtt.broker, broker->valuestring, sizeof(config.mqtt.broker) - 1); mqtt_changed = true; }
        if (port && cJSON_IsNumber(port)) { config.mqtt.port = port->valueint; mqtt_changed = true; }
        if (topic && cJSON_IsString(topic)) { strncpy(config.mqtt.topic, topic->valuestring, sizeof(config.mqtt.topic) - 1); mqtt_changed = true; }
        if (user && cJSON_IsString(user)) { strncpy(config.mqtt.user, user->valuestring, sizeof(config.mqtt.user) - 1); mqtt_changed = true; }
        // Only update password if it's not empty
        if (password && cJSON_IsString(password) && strlen(password->valuestring) > 0) {
            strncpy(config.mqtt.password, password->valuestring, sizeof(config.mqtt.password) - 1);
            mqtt_changed = true;
        }
        if (gnss_interval && cJSON_IsNumber(gnss_interval)) { config.mqtt.gnss_interval_sec = gnss_interval->valueint; mqtt_changed = true; }
        if (status_interval && cJSON_IsNumber(status_interval)) { config.mqtt.status_interval_sec = status_interval->valueint; mqtt_changed = true; }
        if (stats_interval && cJSON_IsNumber(stats_interval)) { config.mqtt.stats_interval_sec = stats_interval->valueint; mqtt_changed = true; }
    }
    
    cJSON_Delete(root);
    
    // Save only what changed to avoid unnecessary reconnections
    esp_err_t err = ESP_OK;
    if (wifi_changed) {
        err = config_set_wifi(&config.wifi);
        if (err != ESP_OK) {
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Failed to save WiFi configuration\"}");
            return ESP_FAIL;
        }
    }
    if (ntrip_changed) {
        err = config_set_ntrip(&config.ntrip);
        if (err != ESP_OK) {
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Failed to save NTRIP configuration\"}");
            return ESP_FAIL;
        }
    }
    if (mqtt_changed) {
        err = config_set_mqtt(&config.mqtt);
        if (err != ESP_OK) {
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Failed to save MQTT configuration\"}");
            return ESP_FAIL;
        }
    }
    
    // Apply WiFi changes ONLY if WiFi was actually changed
    if (wifi_changed && strlen(config.wifi.ssid) > 0) {
        wifi_manager_connect_sta(config.wifi.ssid, config.wifi.password);
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Configuration saved successfully\"}");
    
    return ESP_OK;
}

/**
 * @brief Handler for GET /api/status
 */
static esp_err_t api_status_get_handler(httpd_req_t *req) {
    wifi_status_t wifi_status;
    wifi_manager_get_status(&wifi_status);

    cJSON *root = cJSON_CreateObject();

    // WiFi status
    cJSON *wifi = cJSON_CreateObject();
    cJSON_AddBoolToObject(wifi, "ap_enabled", wifi_status.ap_enabled);
    cJSON_AddBoolToObject(wifi, "sta_connected", wifi_status.sta_connected);
    cJSON_AddStringToObject(wifi, "sta_ip", wifi_status.sta_ip);
    cJSON_AddNumberToObject(wifi, "rssi", wifi_status.rssi);
    cJSON_AddItemToObject(root, "wifi", wifi);

    // NTRIP and MQTT status (live flags)
    extern bool ntrip_client_is_connected(void);
    extern bool mqtt_is_connected(void);
    cJSON_AddBoolToObject(root, "ntrip_connected", ntrip_client_is_connected());
    cJSON_AddBoolToObject(root, "mqtt_connected", mqtt_is_connected());

    // System status
    cJSON *system = cJSON_CreateObject();
    cJSON_AddNumberToObject(system, "uptime_sec", esp_timer_get_time() / 1000000);
    cJSON_AddNumberToObject(system, "free_heap", esp_get_free_heap_size());
    cJSON_AddItemToObject(root, "system", system);

    char *json_string = cJSON_Print(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_string);

    free(json_string);
    cJSON_Delete(root);

    return ESP_OK;
}

/**
 * @brief Handler for POST /api/toggle
 */
static esp_err_t api_toggle_post_handler(httpd_req_t *req) {
    char content[128];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    
    if (ret <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"No data received\"}");
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    // Parse JSON: {"service": "ntrip|mqtt", "enabled": true|false}
    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return ESP_FAIL;
    }
    
    cJSON *service_item = cJSON_GetObjectItem(root, "service");
    cJSON *enabled_item = cJSON_GetObjectItem(root, "enabled");
    
    if (!service_item || !cJSON_IsString(service_item) || !enabled_item || !cJSON_IsBool(enabled_item)) {
        cJSON_Delete(root);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Missing service or enabled field\"}");
        return ESP_FAIL;
    }
    
    const char *service = service_item->valuestring;
    bool enabled = cJSON_IsTrue(enabled_item);
    
    // Update the specific service runtime-only (no NVS write) for instant effect
    esp_err_t err = ESP_OK;
    if (strcmp(service, "ntrip") == 0) {
        err = config_set_ntrip_enabled_runtime(enabled);
        ESP_LOGI(TAG, "NTRIP service %s via web interface (runtime apply)", enabled ? "enabled" : "disabled");
    } else if (strcmp(service, "mqtt") == 0) {
        err = config_set_mqtt_enabled_runtime(enabled);
        ESP_LOGI(TAG, "MQTT service %s via web interface (runtime apply)", enabled ? "enabled" : "disabled");
    } else {
        cJSON_Delete(root);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Unknown service\"}");
        return ESP_FAIL;
    }
    
    cJSON_Delete(root);
    
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Failed to apply toggle\"}");
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Service toggled successfully\"}");
    
    return ESP_OK;
}

/**
 * @brief Handler for POST /api/restart
 */
static esp_err_t api_restart_post_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Device restarting in 3 seconds\"}");
    
    ESP_LOGI(TAG, "Restart requested via web interface");
    
    // Delay and restart
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    
    return ESP_OK;
}

/**
 * @brief Handler for POST /api/factory_reset
 */
static esp_err_t api_factory_reset_post_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Factory reset initiated\"}");
    
    ESP_LOGI(TAG, "Factory reset requested via web interface");
    
    // Perform factory reset
    config_factory_reset();
    
    // Delay and restart
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    
    return ESP_OK;
}

esp_err_t http_server_start(void) {
    if (server != NULL) {
        ESP_LOGW(TAG, "HTTP server already running");
        return ESP_OK;
    }
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 10;
    config.max_open_sockets = 7;
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    
    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);
    
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }
    
    // Register URI handlers
    httpd_uri_t uri_root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_root);
    
    httpd_uri_t uri_api_config_get = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = api_config_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_api_config_get);
    
    httpd_uri_t uri_api_config_post = {
        .uri = "/api/config",
        .method = HTTP_POST,
        .handler = api_config_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_api_config_post);
    
    httpd_uri_t uri_api_status = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = api_status_get_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_api_status);
    
    httpd_uri_t uri_api_toggle = {
        .uri = "/api/toggle",
        .method = HTTP_POST,
        .handler = api_toggle_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_api_toggle);
    
    httpd_uri_t uri_api_restart = {
        .uri = "/api/restart",
        .method = HTTP_POST,
        .handler = api_restart_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_api_restart);
    
    httpd_uri_t uri_api_factory_reset = {
        .uri = "/api/factory_reset",
        .method = HTTP_POST,
        .handler = api_factory_reset_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_api_factory_reset);
    
    ESP_LOGI(TAG, "HTTP server started successfully");
    ESP_LOGI(TAG, "Access web interface at: http://192.168.4.1");
    
    return ESP_OK;
}

esp_err_t http_server_stop(void) {
    if (server == NULL) {
        ESP_LOGW(TAG, "HTTP server not running");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping HTTP server");
    esp_err_t err = httpd_stop(server);
    if (err == ESP_OK) {
        server = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
    
    return err;
}

bool http_server_is_running(void) {
    return (server != NULL);
}
