// --------------------------------------------------------------------
//   This file is part of the PE1MEW NTRIP Client.
//
//   The NTRIP Client is distributed in the hope that 
//   it will be useful, but WITHOUT ANY WARRANTY; without even the 
//   implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
//   PURPOSE.
// --------------------------------------------------------------------*/

/*!
   
 \file NTRIPClient.cpp
 \brief NTRIPClient class implementation
 \author Remko Welling (PE1MEW) 

 This file was retrieved from the following work:
  - [NTRIP-client-for-Arduino](https://github.com/GLAY-AK2/NTRIP-client-for-Arduino)

 The file was refactored to be used in the PE1MEW NTRIP Client project.

*/

#include "NTRIPClient.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <cstdio>

static const char* TAG = "NTRIPClient";

NTRIPClient::NTRIPClient() 
    : client(nullptr), buffer(nullptr), buffer_size(2048), 
      buffer_pos(0), connected_flag(false) {
    buffer = new char[buffer_size];
}

NTRIPClient::~NTRIPClient() {
    disconnect();
    if (buffer) {
        delete[] buffer;
    }
}

bool NTRIPClient::init() {
    return true; // Configuration done per-request
}

bool NTRIPClient::base64Encode(const char* input, char* output, size_t output_size) {
    size_t olen = 0;
    int ret = mbedtls_base64_encode(
        (unsigned char*)output, output_size, &olen,
        (const unsigned char*)input, strlen(input)
    );
    if (ret == 0) {
        output[olen] = '\0';
        return true;
    }
    return false;
}

bool NTRIPClient::reqSrcTblNoAuth(const char* host, int &port) {
    char url[256];
    snprintf(url, sizeof(url), "http://%s:%d/", host, port);

    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 10000;
    config.buffer_size = 2048;

    client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return false;
    }

    esp_http_client_set_header(client, "User-Agent", "NTRIPClient ESP32 v1.0");
    esp_http_client_set_header(client, "Accept", "*/*");

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        client = nullptr;
        return false;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    ESP_LOGI(TAG, "HTTP Status = %d, content_length = %d", status_code, content_length);

    char response[50];
    int read_len = esp_http_client_read(client, response, sizeof(response) - 1);
    if (read_len > 0) {
        response[read_len] = '\0';
        ESP_LOGI(TAG, "Response: %s", response);
        
        if (strncmp(response, "SOURCETABLE 200 OK", 18) == 0) {
            connected_flag = true;
            return true;
        }
    }

    disconnect();
    return false;
}

bool NTRIPClient::reqSrcTbl(const char* host, int &port, const char* user, const char* psw) {
    char url[256];
    snprintf(url, sizeof(url), "http://%s:%d/", host, port);

    // Encode username:password in Base64
    char auth_input[128];
    snprintf(auth_input, sizeof(auth_input), "%s:%s", user, psw);
    char auth_encoded[256];
    if (!base64Encode(auth_input, auth_encoded, sizeof(auth_encoded))) {
        ESP_LOGE(TAG, "Failed to encode credentials");
        return false;
    }

    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 10000;
    config.buffer_size = 2048;

    client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return false;
    }

    char auth_header[300];
    snprintf(auth_header, sizeof(auth_header), "Basic %s", auth_encoded);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "User-Agent", "NTRIPClient ESP32 v1.0");
    esp_http_client_set_header(client, "Accept", "*/*");

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        client = nullptr;
        return false;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    ESP_LOGI(TAG, "HTTP Status = %d, content_length = %d", status_code, content_length);

    char response[50];
    int read_len = esp_http_client_read(client, response, sizeof(response) - 1);
    if (read_len > 0) {
        response[read_len] = '\0';
        ESP_LOGI(TAG, "Response: %s", response);
        
        if (strncmp(response, "SOURCETABLE 200 OK", 18) == 0) {
            connected_flag = true;
            return true;
        }
    }

    disconnect();
    return false;
}

bool NTRIPClient::reqRaw(const char* host, int &port, const char* mntpnt, const char* user, const char* psw) {
    char url[256];
    snprintf(url, sizeof(url), "http://%s:%d/%s", host, port, mntpnt);

    ESP_LOGI(TAG, "Requesting NTRIP mountpoint: %s", mntpnt);

    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 20000;
    config.buffer_size = 2048;
    config.is_async = false;
    config.disable_auto_redirect = true;

    client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return false;
    }

    esp_http_client_set_header(client, "User-Agent", "NTRIPClient ESP32 v1.0");
    esp_http_client_set_header(client, "Accept", "*/*");
    esp_http_client_set_header(client, "Ntrip-Version", "Ntrip/2.0");

    // Add authentication if provided
    if (strlen(user) > 0) {
        char auth_input[128];
        snprintf(auth_input, sizeof(auth_input), "%s:%s", user, psw);
        char auth_encoded[256];
        if (base64Encode(auth_input, auth_encoded, sizeof(auth_encoded))) {
            char auth_header[300];
            snprintf(auth_header, sizeof(auth_header), "Basic %s", auth_encoded);
            esp_http_client_set_header(client, "Authorization", auth_header);
        }
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        client = nullptr;
        return false;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);

    ESP_LOGI(TAG, "HTTP Status = %d, content_length = %d", status_code, content_length);

    // Check for ICY 200 OK response (NTRIP specific)
    char response[50];
    int read_len = esp_http_client_read(client, response, sizeof(response) - 1);
    if (read_len > 0) {
        response[read_len] = '\0';
        
        // For streaming responses, status 200 is good enough
        if (status_code == 200 || strncmp(response, "ICY 200 OK", 10) == 0) {
            connected_flag = true;
            buffer_pos = 0;
            ESP_LOGI(TAG, "Successfully connected to NTRIP stream");
            return true;
        } else {
            ESP_LOGE(TAG, "Unexpected response: %s", response);
        }
    }

    disconnect();
    return false;
}

bool NTRIPClient::reqRaw(const char* host, int &port, const char* mntpnt) {
    return reqRaw(host, port, mntpnt, "", "");
}

int NTRIPClient::readLine(char* _buffer, int size) {
    if (!client || !connected_flag) {
        return 0;
    }

    int len = 0;
    int read_len = esp_http_client_read(client, _buffer, size - 1);
    
    if (read_len > 0) {
        // Find newline character
        for (int i = 0; i < read_len; i++) {
            if (_buffer[i] == '\n') {
                len = i + 1;
                break;
            }
        }
        if (len == 0) len = read_len;
        _buffer[len] = '\0';
    }

    return len;
}

void NTRIPClient::sendGGA(const char* gga) {
    if (!client || !connected_flag) {
        ESP_LOGW(TAG, "Not connected to NTRIP Caster");
        return;
    }

    char ggaString[256];
    snprintf(ggaString, sizeof(ggaString), "%s\r\n", gga);
    
    int written = esp_http_client_write(client, ggaString, strlen(ggaString));
    if (written < 0) {
        ESP_LOGE(TAG, "Failed to send GGA sentence");
    } else {
        ESP_LOGD(TAG, "Sent GGA: %s", gga);
    }
}

bool NTRIPClient::isConnected() {
    return connected_flag && client != nullptr;
}

void NTRIPClient::disconnect() {
    if (client) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        client = nullptr;
    }
    connected_flag = false;
}

int NTRIPClient::readData(uint8_t* data, size_t size) {
    if (!client || !connected_flag) {
        return 0;
    }

    int read_len = esp_http_client_read(client, (char*)data, size);
    if (read_len < 0) {
        ESP_LOGE(TAG, "Error reading data: %d", read_len);
        // Mark as disconnected on read error
        connected_flag = false;
        return -1;  // Return error instead of 0
    }
    
    return read_len;
}

int NTRIPClient::available() {
    if (!client || !connected_flag) {
        return 0;
    }
    
    // ESP-IDF doesn't provide a direct way to check available bytes
    // Try reading a small chunk to check
    return esp_http_client_is_complete_data_received(client) ? 0 : 1;
}
