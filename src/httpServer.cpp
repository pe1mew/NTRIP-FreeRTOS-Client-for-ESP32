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
#include "gnssReceiverTask.h"

// Simple session token (static for now)
static const char* SESSION_TOKEN = "esp_session_token_123";

// HTML content for main configuration page
/**
 * @brief Handler for POST /api/login
 *        Expects JSON: { "password": "..." }
 *        Returns: { "status": "ok", "token": "..." } or { "status": "error", "message": "..." }
 */
static esp_err_t api_login_post_handler(httpd_req_t *req) {
    char content[128];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"No data received\"}");
        return ESP_FAIL;
    }
    content[ret] = '\0';
    cJSON *root = cJSON_Parse(content);
    if (!root) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
        return ESP_FAIL;
    }
    cJSON *pw_item = cJSON_GetObjectItem(root, "password");
    if (!pw_item || !cJSON_IsString(pw_item)) {
        cJSON_Delete(root);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Missing password field\"}");
        return ESP_FAIL;
    }
    const char* password = pw_item->valuestring;
    bool ok = config_test_ui_password(password);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    if (ok) {
        httpd_resp_sendstr(req, "{\"status\":\"ok\",\"token\":\"esp_session_token_123\"}");
    } else {
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Invalid password\"}");
    }
    return ESP_OK;
}
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
"        h2 { color: #ec0d5d; margin-top: 30px; }\n"
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
"    <div id='connection-lost-popup' style='display:none;position:fixed;top:0;left:0;width:100vw;height:100vh;background:rgba(0,0,0,0.4);z-index:9999;align-items:center;justify-content:center;'>\n"
"        <div style='background:white;padding:32px 40px;border-radius:8px;box-shadow:0 4px 16px rgba(0,0,0,0.2);font-size:1.3em;color:#e74c3c;text-align:center;'>\n"
"            <b>Connection to the ESP server is lost.</b><br>Please check your network.\n"
"        </div>\n"
"    </div>\n"
"    <div id='login-container' class='container' style='max-width:400px;display:none;'>\n"
"        <h1>NTRIP-client Configuration</h1>\n"
"        <div id='login-status'></div>\n"
"        <div class='form-group'>\n"
"            <label for='login-password'>Login with UI Password:</label>\n"
"            <input type='password' id='login-password' maxlength='63'>\n"
"        </div>\n"
"        <button onclick='login()'>Login</button>\n"
"    </div>\n"
"    <div id='main-container' class='container' style='display:none;'>\n"
"        <h1>NTRIP-client Configuration</h1>\n"
"        <div id='status'></div>\n"
"        <h2>GNSS receiver</h2>\n"
"        <div class='status-indicator-container' style='display: flex; align-items: center; margin-bottom: 8px;'>\n"
"            <span style='color:#555; font-size:15px; font-weight:bold; margin-right:6px;'>Status:</span>\n"
"            <span id='gnss_status_indicator' class='status-indicator disconnected' title='Valid NMEA'></span>\n"
"            <span style='color:#555; font-size:15px; margin-left:18px;'>Satellites: <span id='gnss_sat_count'>0</span></span>\n"
"        </div>\n"
"        <div class='status-indicator-container' style='display: flex; align-items: center; margin-bottom: 15px;'>\n"
"            <span style='color:#555; font-size:15px; font-weight:bold; margin-right:10px;'>Position quality:</span>\n"
"            <span id='gnss_signal_bar' style='display: flex; align-items: center;'>\n"
"                <span class='gnss-bar-box' id='gnss-bar-1'></span>\n"
"                <span class='gnss-bar-box' id='gnss-bar-2'></span>\n"
"                <span class='gnss-bar-box' id='gnss-bar-3'></span>\n"
"                <span class='gnss-bar-box' id='gnss-bar-4'></span>\n"
"            </span>\n"
"            <span id='gnss_quality_text' style='color:#0066cc; font-size:14px; margin-left:14px;'></span>\n"
"        </div>\n"
"        <style>\n"
"        .gnss-bar-box { width: 18px; height: 18px; border: 2px solid #0066cc; margin-right: 3px; display: inline-block; border-radius: 3px; background: #fff; transition: background 0.2s, border-color 0.2s; }\n"
"        .gnss-bar-box.filled { background: #0066cc; border-color: #0066cc; }\n"
"        </style>\n"
"        <h2>UI Password</h2>\n"
"        <div id='ui_password_warning' class='status error' style='display:none;'>\n"
"            <b>Warning:</b> The UI password is still set to the factory default. Please change it for security.\n"
"        </div>\n"
"        <div class='form-group'>\n"
"            <label>UI Password:</label>\n"
"            <input type='password' id='ui_password' maxlength='63' placeholder='Leave blank to keep current password'>\n"
"        </div>\n"
"        <h2>WiFi Configuration</h2>\n"
"        <div class='status-indicator-container' style='display: flex; align-items: center; margin-bottom: 15px;'>\n"
"            <span style='color:#555; font-size:15px; font-weight:bold; margin-right:6px;'>Status:</span>\n"
"            <span id='wifi_status_indicator' class='status-indicator disconnected' title='WiFi status'></span>\n"
"            <span style='color:#555; font-size:15px; margin-left:18px;'>STA IP: <span id='sta_ip_display'>Checking...</span></span>\n"
"        </div>\n"
"        <div class='form-group'>\n"
"            <label>STA SSID:</label>\n"
"            <input type='text' id='wifi_ssid' maxlength='31'>\n"
"        </div>\n"
"        <div class='form-group'>\n"
"            <label>STA Password:</label>\n"
"            <input type='password' id='wifi_password' maxlength='63' placeholder='Leave blank to keep current password'>\n"
"        </div>\n"
"        <div class='form-group'>\n"
"            <label>AP SSID:</label>\n"
"            <input type='text' id='ap_ssid' maxlength='31' readonly style='background:#f5f5f5;'>\n"
"        </div>\n"
"        <div class='form-group'>\n"
"            <label>AP Password:</label>\n"
"            <input type='password' id='ap_password' maxlength='63' placeholder='Leave blank to keep current password'>\n"
"        </div>\n"
"        <h2 style='margin-bottom: 8px;'>NTRIP Configuration</h2>\n"
"        <div class='status-indicator-container' style='display: flex; align-items: center; margin-bottom: 15px;'>\n"
"            <span style='font-weight:bold; color:#555; margin-right:10px;'>Enable:</span>\n"
"            <label class='switch'>\n"
"                <input type='checkbox' id='ntrip_enabled' onchange='toggleService(\"ntrip\", this.checked)'>\n"
"                <span class='slider'></span>\n"
"            </label>\n"
"            <span style='color:#555; font-size:15px; font-weight:bold; margin-left:18px; margin-right:6px;'>Status:</span>\n"
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
"        <div class='status-indicator-container' style='display: flex; align-items: center; margin-bottom: 15px;'>\n"
"            <span style='font-weight:bold; color:#555; margin-right:10px;'>Enable:</span>\n"
"            <label class='switch'>\n"
"                <input type='checkbox' id='mqtt_enabled' onchange='toggleService(\"mqtt\", this.checked)'>\n"
"                <span class='slider'></span>\n"
"            </label>\n"
"            <span style='color:#555; font-size:15px; font-weight:bold; margin-left:18px; margin-right:6px;'>Status:</span>\n"
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
"            <button onclick='logout()'>Logout</button>\n"
"        </div>\n"
"    </div>\n"
"    <div style='margin-top: 30px; margin-bottom: 10px;'>\n"
"        <!-- BEGIN MadeByESE SVG -->\n"
"        <svg width=\"250\" viewBox=\"0 0 1572 287\" style=\"display:block; height:auto; margin-left:0; padding-left:0;\" xmlns=\"http://www.w3.org/2000/svg\" xmlns:svg=\"http://www.w3.org/2000/svg\">\n"
"           <g id=\"g5\" transform=\"matrix(4.1801072,0,0,4.1801072,-34.299529,-314.34125)\" style=\"stroke-width:1.00142;stroke-dasharray:none\">\n"
"              <g id=\"layer1-7\" style=\"stroke-width:1.00142;stroke-dasharray:none\">\n"
"                 <rect style=\"fill:#ec0d5d;fill-opacity:1;stroke:none;stroke-width:1.00142;stroke-miterlimit:1;stroke-dasharray:none;stroke-opacity:1\" id=\"rect1\" width=\"375.88104\" height=\"68.691734\" x=\"8.2983532\" y=\"75.182724\" />\n"
"                 <path d=\"m 141.65486,105.89488 q 0.54934,-2.04752 2.09747,-3.44583 1.59807,-1.44825 4.09505,-1.44825 1.34837,0 2.34716,0.39952 0.9988,0.39951 1.69795,1.09867 0.69916,0.6492 1.14861,1.54813 0.4994,0.8989 0.7491,1.84776 h 0.0999 q 0.29964,-0.89892 0.84897,-1.74788 0.59928,-0.89893 1.39831,-1.59808 0.84898,-0.69916 1.94765,-1.09867 1.09867,-0.44946 2.49698,-0.44946 2.09747,0 3.34596,0.89891 1.29843,0.84898 1.99759,2.29723 0.69915,1.44825 0.94885,3.34595 0.2497,1.84777 0.2497,3.79543 V 127.319 h -5.64318 v -15.98069 q 0,-0.94886 -0.0999,-1.89771 -0.0999,-0.9988 -0.44946,-1.79784 -0.29964,-0.79903 -0.94885,-1.24849 -0.59928,-0.49939 -1.59807,-0.49939 -1.89771,0 -3.04632,1.8977 -1.09867,1.89772 -1.09867,4.79422 v 14.7322 h -5.54331 v -15.98069 q 0,-0.94886 -0.0999,-1.89771 -0.0999,-0.9988 -0.44945,-1.79784 -0.29964,-0.79903 -0.89892,-1.24849 -0.59927,-0.49939 -1.59807,-0.49939 -1.8977,0 -2.99637,1.8977 -1.04874,1.89772 -1.04874,4.74428 V 127.319 h -5.59324 v -25.46923 h 5.5433 v 4.04511 z m 42.39877,9.48854 h -1.64801 q -1.04873,0 -2.04753,0.19976 -0.94885,0.14982 -1.69794,0.64921 -0.7491,0.4994 -1.19856,1.39831 -0.44945,0.84898 -0.44945,2.19735 0,1.99759 0.84897,2.94644 0.89891,0.94885 2.04753,0.94885 1.14861,0 1.94764,-0.64921 0.79904,-0.64922 1.2485,-1.64801 0.49939,-0.99879 0.69915,-2.19735 0.2497,-1.24849 0.2497,-2.3971 z m 0.19976,8.14016 q -0.54934,2.09747 -1.99759,3.29602 -1.44825,1.14861 -3.84535,1.14861 -1.09868,0 -2.24729,-0.39951 -1.14861,-0.39952 -2.09746,-1.29843 -0.89892,-0.94886 -1.49819,-2.39711 -0.59928,-1.49819 -0.59928,-3.59565 0,-2.69675 0.94886,-4.39469 0.99879,-1.74789 2.49698,-2.74668 1.54813,-0.9988 3.39589,-1.34838 1.89771,-0.39951 3.69554,-0.39951 h 1.39831 v -0.79904 q 0,-2.54693 -1.04873,-3.6456 -0.9988,-1.14861 -2.69674,-1.14861 -1.44825,0 -2.64681,0.69915 -1.14861,0.64922 -1.99758,1.74789 l -2.69674,-3.54572 q 1.39831,-1.74787 3.54571,-2.69674 2.14741,-0.99879 4.24487,-0.99879 2.34717,0 3.94524,0.74909 1.64801,0.7491 2.6468,2.19735 0.99879,1.39831 1.39831,3.49577 0.44946,2.09748 0.44946,4.74428 V 127.319 h -4.69433 v -3.79542 z m 23.72131,3.79542 v -3.6456 h -0.0999 q -0.54934,1.89771 -1.89771,3.19614 -1.29843,1.29843 -3.79541,1.29843 -1.94765,0 -3.49578,-1.04873 -1.54813,-1.04873 -2.59686,-2.8965 -1.04873,-1.89771 -1.59807,-4.39469 -0.54933,-2.54692 -0.54933,-5.49336 0,-2.8965 0.49939,-5.34355 0.54934,-2.44704 1.59807,-4.19494 1.04873,-1.79783 2.54692,-2.79662 1.54813,-0.99879 3.59566,-0.99879 2.09747,0 3.54572,1.19855 1.44825,1.14861 1.99758,2.8965 h 0.14982 V 89.564614 h 5.5433 V 127.319 Z m 0.0999,-12.73461 q 0,-1.84777 -0.29964,-3.3959 -0.2497,-1.59807 -0.79903,-2.74668 -0.4994,-1.19855 -1.34837,-1.84777 -0.79904,-0.69916 -1.84777,-0.69916 -1.09867,0 -1.89771,0.69916 -0.79903,0.64922 -1.29843,1.84777 -0.49939,1.14861 -0.74909,2.74668 -0.2497,1.54813 -0.2497,3.3959 0,1.79782 0.2497,3.39589 0.2497,1.54813 0.74909,2.74668 0.4994,1.14862 1.29843,1.84777 0.79904,0.64922 1.89771,0.64922 1.09867,0 1.89771,-0.64922 0.79903,-0.69922 1.29843,-1.84777 0.54933,-1.19855 0.79903,-2.74668 0.29964,-1.59807 0.29964,-3.39589 z m 16.18045,1.44825 q 0,1.49819 0.29964,2.84656 0.29964,1.29843 0.84898,2.29722 0.59927,0.94885 1.39831,1.54813 0.84897,0.54934 1.94764,0.54934 1.54813,0 2.44705,-0.9988 0.94885,-0.99879 1.49819,-2.29722 l 4.14499,2.3971 q -1.09867,2.6468 -3.1462,4.24487 -1.99759,1.54813 -5.14378,1.54813 -4.44463,0 -7.14138,-3.49577 -2.69673,-3.49578 -2.69673,-10.03787 0,-3.04632 0.69915,-5.54331 0.69916,-2.54692 1.99758,-4.34475 1.29844,-1.79782 3.04632,-2.74668 1.79783,-0.99879 3.99518,-0.99879 2.44704,0 4.14499,0.99879 1.74789,0.99879 2.79662,2.74668 1.09867,1.69795 1.59807,4.04512 0.49939,2.34716 0.49939,5.09384 v 2.14741 z m 7.84053,-4.09506 q 0,-2.84656 -0.89891,-4.59445 -0.89892,-1.79783 -2.79662,-1.79783 -1.09868,0 -1.89771,0.64921 -0.7491,0.64922 -1.24849,1.64801 -0.44946,0.94886 -0.69916,2.09748 -0.2497,1.09867 -0.2497,1.99758 z m 20.27549,-22.372965 h 5.64318 V 105.0459 h 0.0999 q 0.54934,-1.74788 1.99759,-2.8965 1.44825,-1.14861 3.54571,-1.14861 1.99759,0 3.54572,0.99879 1.54813,0.99879 2.59686,2.79662 1.04874,1.7479 1.54813,4.19494 0.54934,2.44705 0.54934,5.34355 0,2.94644 -0.54934,5.49336 -0.54933,2.49698 -1.59807,4.39469 -1.04873,1.84777 -2.59686,2.8965 -1.54813,1.04873 -3.49578,1.04873 -2.49698,0 -3.79541,-1.29843 -1.29843,-1.29843 -1.89771,-3.19614 h -0.0999 v 3.6456 h -5.49336 z m 5.44343,25.019775 q 0,1.79782 0.24969,3.39589 0.29964,1.54813 0.79904,2.74668 0.54933,1.14862 1.34837,1.84777 0.79903,0.64922 1.84777,0.64922 1.09867,0 1.84776,-0.64922 0.79904,-0.69915 1.29843,-1.84777 0.4994,-1.19855 0.7491,-2.74668 0.2497,-1.59807 0.2497,-3.39589 0,-1.79783 -0.2497,-3.3959 -0.2497,-1.59807 -0.7491,-2.74668 -0.49939,-1.19855 -1.29843,-1.84777 -0.74909,-0.69916 -1.84776,-0.69916 -1.04874,0 -1.84777,0.69916 -0.79904,0.64922 -1.34837,1.84777 -0.4994,1.14861 -0.79904,2.74668 -0.24969,1.59807 -0.24969,3.3959 z m 29.21472,16.23038 q -0.49939,1.89771 -1.19855,3.44584 -0.64921,1.59807 -1.64801,2.69674 -0.99879,1.14861 -2.3971,1.74789 -1.34837,0.59927 -3.19614,0.59927 -0.99879,0 -1.94764,-0.14981 -0.89892,-0.0999 -1.49819,-0.29964 l 0.69915,-5.09385 q 0.39952,0.14982 0.94885,0.2497 0.59928,0.14982 1.14862,0.14982 1.34837,0 2.04752,-0.84897 0.69916,-0.79904 1.04874,-2.24729 l 0.74909,-3.1462 -7.44101,-26.0685 h 5.94282 l 4.44463,19.17683 h 0.14982 l 4.04511,-19.17683 h 5.74306 z M 308.50326,127.319 V 91.961715 h 15.83087 v 5.2936 H 314.2463 v 9.388665 h 9.33871 v 4.89409 h -9.33871 v 10.43738 h 10.58721 V 127.319 Z M 344.1602,98.803445 q -1.94764,-2.34716 -4.89408,-2.34716 -0.84898,0 -1.64801,0.29964 -0.79904,0.2497 -1.44825,0.848969 -0.64922,0.59928 -1.04874,1.54813 -0.34957,0.948846 -0.34957,2.247296 0,2.24728 1.34837,3.49576 1.34837,1.19856 3.54571,2.29724 1.29843,0.6492 2.6468,1.49818 1.34838,0.84898 2.44705,2.04753 1.09867,1.19855 1.79783,2.84656 0.69915,1.64801 0.69915,3.8953 0,2.59686 -0.84897,4.59444 -0.79904,1.99759 -2.19735,3.3959 -1.34837,1.34837 -3.19614,2.04753 -1.84776,0.69915 -3.89529,0.69915 -2.94644,0 -5.44342,-1.14861 -2.49699,-1.14861 -3.99518,-2.8965 l 3.24608,-4.39469 q 1.14861,1.39831 2.74668,2.19734 1.64801,0.79904 3.34596,0.79904 1.89771,0 3.1462,-1.29843 1.24849,-1.29843 1.24849,-3.6456 0,-2.29722 -1.49819,-3.64559 -1.49819,-1.34837 -3.79541,-2.49698 -1.44825,-0.69916 -2.74668,-1.54813 -1.2485,-0.84898 -2.24729,-1.99759 -0.94885,-1.19855 -1.54813,-2.74669 -0.54933,-1.59807 -0.54933,-3.79541 0,-2.746685 0.84897,-4.694325 0.89891,-1.99759 2.29722,-3.29602 1.44825,-1.29843 3.24608,-1.89771 1.79783,-0.64921 3.6456,-0.64921 2.69674,0 4.7942,0.89891 2.14741,0.84897 3.59566,2.54692 z M 352.69985,127.319 V 91.961715 h 15.83088 v 5.2936 h -10.08782 v 9.388665 h 9.33872 v 4.89409 h -9.33872 v 10.43738 h 10.58721 v 5.34355 z\" style=\"font-size:50.8px;line-height:1.25;font-family:'Avenir Next Condensed';fill:#fefefe;stroke:#ffffff;stroke-width:1.00142;\"/>\n"
"              </g>\n"
"           </g>\n"
"        </svg>\n"
"        <!-- END MadeByESE SVG -->\n"
"    </div>\n"
"    <script>\n"
"        // --- Authentication logic ---\n"
"        function showLogin() {\n"
"            document.getElementById('login-container').style.display = 'block';\n"
"            document.getElementById('main-container').style.display = 'none';\n"
"        }\n"
"        function showMain() {\n"
"            document.getElementById('login-container').style.display = 'none';\n"
"            document.getElementById('main-container').style.display = 'block';\n"
"        }\n"
"        function login() {\n"
"            const pw = document.getElementById('login-password').value;\n"
"            fetch('/api/login', {\n"
"                method: 'POST',\n"
"                headers: {'Content-Type': 'application/json'},\n"
"                body: JSON.stringify({password: pw})\n"
"            })\n"
"            .then(r => r.json())\n"
"            .then(data => {\n"
"                if (data.status === 'ok') {\n"
"                    localStorage.setItem('session_token', data.token);\n"
"                    showMain();\n"
"                    loadConfig();\n"
"                    updateStatus();\n"
"                } else {\n"
"                    showLoginStatus(data.message || 'Login failed', 'error');\n"
"                }\n"
"            })\n"
"            .catch(e => showLoginStatus('Login failed', 'error'));\n"
"        }\n"
"        function showLoginStatus(msg, type) {\n"
"            const div = document.getElementById('login-status');\n"
"            div.className = 'status ' + type;\n"
"            div.textContent = msg;\n"
"            setTimeout(() => div.textContent = '', 4000);\n"
"        }\n"
"        function logout() {\n"
"            localStorage.removeItem('session_token');\n"
"            showLogin();\n"
"        }\n"
"        function getAuthHeaders() {\n"
"            const token = localStorage.getItem('session_token');\n"
"            return token ? { 'Authorization': 'Bearer ' + token } : {};\n"
"        }\n"
"        // --- Existing functions ---\n"
"        function loadConfig() {\n"
"            fetch('/api/config', { headers: getAuthHeaders() }).then(r => {\n"
"                if (r.status === 401) { logout(); return Promise.reject('Unauthorized'); }\n"
"                return r.json();\n"
"            }).then(data => {\n"
"                document.getElementById('ui_password').value = '';\n"
"                // Show/hide UI password warning\n"
"                if (data.ui && data.ui.password_is_default) {\n"
"                    document.getElementById('ui_password_warning').style.display = 'block';\n"
"                } else {\n"
"                    document.getElementById('ui_password_warning').style.display = 'none';\n"
"                }\n"
"                document.getElementById('wifi_ssid').value = data.wifi.ssid;\n"
"                document.getElementById('ap_ssid').value = data.wifi.ap_ssid || 'NTRIPClient';\n"
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
"            const topic = document.getElementById('mqtt_topic').value;\n"
"            if (topic.length === 0 || topic.startsWith('/') || topic.endsWith('/')) {\n"
"                showStatus('MQTT topic must not start or end with a slash.', 'error');\n"
"                return;\n"
"            }\n"
"            const config = {\n"
"                ui: { password: document.getElementById('ui_password').value },\n"
"                wifi: { ssid: document.getElementById('wifi_ssid').value, password: document.getElementById('wifi_password').value, ap_password: document.getElementById('ap_password').value },\n"
"                ntrip: { enabled: document.getElementById('ntrip_enabled').checked, host: document.getElementById('ntrip_host').value,\n"
"                         port: parseInt(document.getElementById('ntrip_port').value), mountpoint: document.getElementById('ntrip_mountpoint').value,\n"
"                         user: document.getElementById('ntrip_user').value, password: document.getElementById('ntrip_password').value,\n"
"                         gga_interval_sec: parseInt(document.getElementById('ntrip_gga_interval').value), reconnect_delay_sec: 5 },\n"
"                mqtt: { enabled: document.getElementById('mqtt_enabled').checked, broker: document.getElementById('mqtt_broker').value,\n"
"                        port: parseInt(document.getElementById('mqtt_port').value), topic: topic,\n"
"                        user: document.getElementById('mqtt_user').value, password: document.getElementById('mqtt_password').value,\n"
"                        gnss_interval_sec: parseInt(document.getElementById('mqtt_gnss_interval').value),\n"
"                        status_interval_sec: parseInt(document.getElementById('mqtt_status_interval').value),\n"
"                        stats_interval_sec: parseInt(document.getElementById('mqtt_stats_interval').value) }\n"
"            };\n"
"            fetch('/api/config', { method: 'POST', headers: Object.assign({'Content-Type': 'application/json'}, getAuthHeaders()), body: JSON.stringify(config) })\n"
"            .then(r => { if (r.status === 401) { logout(); return Promise.reject('Unauthorized'); } return r.json(); })\n"
"            .then(data => showStatus(data.message, data.status === 'ok' ? 'success' : 'error'))\n"
"            .catch(e => showStatus('Failed to save configuration', 'error'));\n"
"        }\n"
"        function restartDevice() {\n"
"            if(confirm('Restart device?')) {\n"
"                fetch('/api/restart', {method: 'POST', headers: getAuthHeaders()}).then(r => { if (r.status === 401) { logout(); return Promise.reject('Unauthorized'); } return r.json(); }).then(() => showStatus('Device restarting...', 'success'));\n"
"            }\n"
"        }\n"
"        function factoryReset() {\n"
"            if(confirm('Reset all settings to factory defaults?')) {\n"
"                fetch('/api/factory_reset', {method: 'POST', headers: getAuthHeaders()}).then(r => { if (r.status === 401) { logout(); return Promise.reject('Unauthorized'); } return r.json(); }).then(() => showStatus('Factory reset complete, device restarting...', 'success'));\n"
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
"            fetch('/api/toggle', { method: 'POST', headers: Object.assign({'Content-Type': 'application/json'}, getAuthHeaders()), body: JSON.stringify(payload) })\n"
"            .then(r => { if (r.status === 401) { logout(); return Promise.reject('Unauthorized'); } return r.json(); })\n"
"            .then(data => {\n"
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
"        let connectionLost = false;\n"
"        function showConnectionLostPopup() {\n"
"            if (!connectionLost) {\n"
"                document.getElementById('connection-lost-popup').style.display = 'flex';\n"
"                connectionLost = true;\n"
"            }\n"
"        }\n"
"        function hideConnectionLostPopup() {\n"
"            if (connectionLost) {\n"
"                document.getElementById('connection-lost-popup').style.display = 'none';\n"
"                connectionLost = false;\n"
"            }\n"
"        }\n"
"        function updateStatus() {\n"
"            fetch('/api/status', { headers: getAuthHeaders() }).then(r => {\n"
"                if (r.status === 401) {\n"
"                    // Still check connection, but do not show connection lost popup for unauthorized\n"
"                    hideConnectionLostPopup();\n"
"                    logout();\n"
"                    return Promise.reject('Unauthorized');\n"
"                }\n"
"                return r.json();\n"
"            }).then(data => {\n"
"                hideConnectionLostPopup();\n"
"                // GNSS indicator\n"
"                var gnss = data.gnss_ok !== undefined ? data.gnss_ok : false;\n"
"                var gnssInd = document.getElementById('gnss_status_indicator');\n"
"                if (gnss) {\n"
"                    gnssInd.classList.add('connected');\n"
"                    gnssInd.classList.remove('disconnected');\n"
"                } else {\n"
"                    gnssInd.classList.remove('connected');\n"
"                    gnssInd.classList.add('disconnected');\n"
"                }\n"
"                document.getElementById('gnss_sat_count').textContent = data.gnss_satellites !== undefined ? data.gnss_satellites : '0';\n"
"                // GNSS signal bar\n"
"                var fix = data.gnss_fix_quality !== undefined ? data.gnss_fix_quality : 0;\n"
"                for (var i = 1; i <= 4; ++i) {\n"
"                    var box = document.getElementById('gnss-bar-' + i);\n"
"                    box.classList.remove('filled');\n"
"                }\n"
"                var qualityText = '';\n"
"                if (gnss) {\n"
"                    if (fix === 4) {\n"
"                        for (var i = 1; i <= 4; ++i) document.getElementById('gnss-bar-' + i).classList.add('filled');\n"
"                        qualityText = 'RTK FIX: Centimeter-level accuracy';\n"
"                    } else if (fix === 5) {\n"
"                        for (var i = 1; i <= 3; ++i) document.getElementById('gnss-bar-' + i).classList.add('filled');\n"
"                        qualityText = 'RTK FLOAT: Decimeter-level accuracy';\n"
"                    } else if (fix === 1 || fix === 2) {\n"
"                        for (var i = 1; i <= 2; ++i) document.getElementById('gnss-bar-' + i).classList.add('filled');\n"
"                        qualityText = 'GPS FIX: Meter-level accuracy';\n"
"                    } else {\n"
"                        document.getElementById('gnss-bar-1').classList.add('filled');\n"
"                        qualityText = 'Valid NMEA, no fix';\n"
"                    }\n"
"                } else {\n"
"                    document.getElementById('gnss-bar-1').classList.add('filled');\n"
"                    qualityText = 'No valid NMEA data';\n"
"                }\n"
"                document.getElementById('gnss_quality_text').textContent = qualityText;\n"
"                // WiFi indicator\n"
"                var wifi = data.wifi.sta_connected !== undefined ? data.wifi.sta_connected : false;\n"
"                var wifiInd = document.getElementById('wifi_status_indicator');\n"
"                if (wifi) {\n"
"                    wifiInd.classList.add('connected');\n"
"                    wifiInd.classList.remove('disconnected');\n"
"                } else {\n"
"                    wifiInd.classList.remove('connected');\n"
"                    wifiInd.classList.add('disconnected');\n"
"                }\n"
"                document.getElementById('sta_ip_display').textContent = data.wifi.sta_ip || 'Not connected';\n"
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
"            }).catch(function(e) {\n"
"                // Only show connection lost popup if error is not 401 Unauthorized\n"
"                if (!(e && typeof e === 'string' && e === 'Unauthorized')) {\n"
"                    showConnectionLostPopup();\n"
"                }\n"
"            });\n"
"        }\n"
"        // Periodically update status every 5 seconds\n"
"        setInterval(updateStatus, 5000);\n"
"        // --- Initial auth check ---\n"
"        if (localStorage.getItem('session_token')) {\n"
"            showMain();\n"
"            loadConfig();\n"
"            updateStatus();\n"
"        } else {\n"
"            showLogin();\n"
"        }\n"
"    </script>\n"
"    </div>\n"
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

// Helper to check session token from Authorization header
static bool check_auth(httpd_req_t *req) {
    char buf[128];
    size_t hlen = httpd_req_get_hdr_value_len(req, "Authorization");
    if (hlen == 0 || hlen >= sizeof(buf)) return false;
    if (httpd_req_get_hdr_value_str(req, "Authorization", buf, sizeof(buf)) != ESP_OK) return false;
    const char *prefix = "Bearer ";
    if (strncmp(buf, prefix, strlen(prefix)) != 0) return false;
    const char *token = buf + strlen(prefix);
    return strcmp(token, SESSION_TOKEN) == 0;
}

/**
 * @brief Handler for GET /api/config
 */
static esp_err_t api_config_get_handler(httpd_req_t *req) {
    if (!check_auth(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Unauthorized\"}");
        return ESP_FAIL;
    }
    app_config_t config;
    if (config_get_all(&config) != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Failed to read configuration\"}");
        return ESP_FAIL;
    }
    // Create JSON response (mask passwords)
    cJSON *root = cJSON_CreateObject();
    // UI config
    cJSON *ui = cJSON_CreateObject();
    cJSON_AddStringToObject(ui, "password", "********");
    // Add a flag if the UI password is still at default
    const char* default_ui_pw = config_get_default_ui_password();
    bool ui_password_is_default = (strcmp(config.ui.password, default_ui_pw) == 0);
    cJSON_AddBoolToObject(ui, "password_is_default", ui_password_is_default);
    cJSON_AddItemToObject(root, "ui", ui);
    // WiFi config
    cJSON *wifi = cJSON_CreateObject();
    cJSON_AddStringToObject(wifi, "ssid", config.wifi.ssid);
    cJSON_AddStringToObject(wifi, "password", "********");
    char ap_ssid[32];
    wifi_manager_get_ap_ssid(ap_ssid, sizeof(ap_ssid));
    cJSON_AddStringToObject(wifi, "ap_ssid", ap_ssid);
    cJSON_AddStringToObject(wifi, "ap_password", "********");
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
    if (!check_auth(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Unauthorized\"}");
        return ESP_FAIL;
    }
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
    
    // Parse UI config
    cJSON *ui = cJSON_GetObjectItem(root, "ui");
    if (ui) {
        cJSON *password = cJSON_GetObjectItem(ui, "password");
        if (password && cJSON_IsString(password) && strlen(password->valuestring) > 0) {
            strncpy(config.ui.password, password->valuestring, sizeof(config.ui.password) - 1);
            // No event bit for UI password, but always save
            config_set_all(&config);
        }
    }
    // Parse WiFi config
    cJSON *wifi = cJSON_GetObjectItem(root, "wifi");
    if (wifi) {
        cJSON *ssid = cJSON_GetObjectItem(wifi, "ssid");
        cJSON *password = cJSON_GetObjectItem(wifi, "password");
        cJSON *ap_password = cJSON_GetObjectItem(wifi, "ap_password");
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
        // Only update AP password if it's not empty
        if (ap_password && cJSON_IsString(ap_password) && strlen(ap_password->valuestring) > 0) {
            strncpy(config.wifi.ap_password, ap_password->valuestring, sizeof(config.wifi.ap_password) - 1);
            wifi_changed = true;
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
        if (topic && cJSON_IsString(topic)) {
            const char *t = topic->valuestring;
            size_t tlen = strlen(t);
            if (tlen == 0 || t[0] == '/' || t[tlen-1] == '/') {
                httpd_resp_set_status(req, "400 Bad Request");
                httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"MQTT topic must not start or end with a slash.\"}");
                cJSON_Delete(root);
                return ESP_FAIL;
            }
            strncpy(config.mqtt.topic, t, sizeof(config.mqtt.topic) - 1);
            mqtt_changed = true;
        }
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
    if (!check_auth(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Unauthorized\"}");
        return ESP_FAIL;
    }
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

    // GNSS status: expose nmea received (valid) and satellite count
    extern void gnss_get_data(gnss_data_t *data);
    extern bool gnss_has_valid_fix(void);
    gnss_data_t gnss;
    gnss_get_data(&gnss);
    bool gnss_ok = gnss_has_valid_fix();
    cJSON_AddBoolToObject(root, "gnss_ok", gnss_ok);
    cJSON_AddNumberToObject(root, "gnss_satellites", gnss.satellites);
    cJSON_AddNumberToObject(root, "gnss_fix_quality", gnss.fix_quality);

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
    if (!check_auth(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Unauthorized\"}");
        return ESP_FAIL;
    }
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
    if (!check_auth(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Unauthorized\"}");
        return ESP_FAIL;
    }
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
    if (!check_auth(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Unauthorized\"}");
        return ESP_FAIL;
    }
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

    // Register /api/login endpoint
    httpd_uri_t uri_api_login = {
        .uri = "/api/login",
        .method = HTTP_POST,
        .handler = api_login_post_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri_api_login);
    
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
