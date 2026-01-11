#include "gnssReceiverTask.h"
#include "ntripClientTask.h"
#include "configurationManagerTask.h"
#include "hardware_config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <string.h>
#include <sys/time.h>

static const char *TAG = "GNSSTask";

// UART Buffer Configuration
#define GNSS_RX_BUF_SIZE   2048
#define GNSS_TX_BUF_SIZE   1024

// Task configuration
#define GNSS_TASK_STACK_SIZE   4096
#define GNSS_TASK_PRIORITY     4
#define GNSS_UART_TIMEOUT_MS   100

// Default GGA interval (seconds)
#define DEFAULT_GGA_INTERVAL_SEC  120

// Global GNSS data and mutex
static gnss_data_t gnss_data = {
    .gga = {0},
    .rmc = {0},
    .vtg = {0},
    .timestamp = 0,
    .valid = false
};
static SemaphoreHandle_t gnss_data_mutex = NULL;
static TaskHandle_t gnss_task_handle = NULL;

// Calculate NMEA checksum
static uint8_t calculate_nmea_checksum(const char *sentence) {
    uint8_t checksum = 0;
    const char *p = sentence;
    
    // Skip '$' if present
    if (*p == '$') p++;
    
    // Calculate XOR of all characters until '*' or end
    while (*p && *p != '*' && *p != '\r' && *p != '\n') {
        checksum ^= *p;
        p++;
    }
    
    return checksum;
}

// Validate NMEA sentence checksum
static bool validate_nmea_sentence(const char *sentence) {
    if (!sentence || sentence[0] != '$') {
        return false;
    }
    
    // Find asterisk
    const char *asterisk = strchr(sentence, '*');
    if (!asterisk || strlen(asterisk) < 3) {
        return false;
    }
    
    // Extract checksum from sentence
    uint8_t stated_checksum;
    if (sscanf(asterisk + 1, "%2hhx", &stated_checksum) != 1) {
        return false;
    }
    
    // Calculate checksum
    uint8_t calculated_checksum = calculate_nmea_checksum(sentence);
    
    return stated_checksum == calculated_checksum;
}

// Check if sentence is of specified type
static bool is_sentence_type(const char *sentence, const char *type) {
    if (!sentence || sentence[0] != '$') {
        return false;
    }
    
    // Check for either GP or GN prefix (GPS or GNSS)
    if (strncmp(sentence + 1, "GP", 2) == 0 || strncmp(sentence + 1, "GN", 2) == 0) {
        return strncmp(sentence + 3, type, strlen(type)) == 0;
    }
    
    return false;
}

// Update GNSS data with new sentence
static void update_gnss_data(const char *sentence) {
    if (!validate_nmea_sentence(sentence)) {
        ESP_LOGD(TAG, "Invalid NMEA checksum");
        return;
    }
    
    if (xSemaphoreTake(gnss_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        
        if (is_sentence_type(sentence, "GGA")) {
            strncpy(gnss_data.gga, sentence, sizeof(gnss_data.gga) - 1);
            gnss_data.gga[sizeof(gnss_data.gga) - 1] = '\0';
            gnss_data.timestamp = tv.tv_sec;
            gnss_data.valid = true;
            ESP_LOGD(TAG, "Updated GGA: %s", gnss_data.gga);
        } else if (is_sentence_type(sentence, "RMC")) {
            strncpy(gnss_data.rmc, sentence, sizeof(gnss_data.rmc) - 1);
            gnss_data.rmc[sizeof(gnss_data.rmc) - 1] = '\0';
            gnss_data.timestamp = tv.tv_sec;
            ESP_LOGD(TAG, "Updated RMC: %s", gnss_data.rmc);
        } else if (is_sentence_type(sentence, "VTG")) {
            strncpy(gnss_data.vtg, sentence, sizeof(gnss_data.vtg) - 1);
            gnss_data.vtg[sizeof(gnss_data.vtg) - 1] = '\0';
            gnss_data.timestamp = tv.tv_sec;
            ESP_LOGD(TAG, "Updated VTG: %s", gnss_data.vtg);
        }
        
        xSemaphoreGive(gnss_data_mutex);
    }
}

// Initialize UART2 for GNSS communication
static esp_err_t init_gnss_uart(void) {
    uart_config_t uart_config = {
        .baud_rate = GNSS_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {}
    };
    
    // Install UART driver
    esp_err_t err = uart_driver_install(GNSS_UART_NUM, GNSS_RX_BUF_SIZE, GNSS_TX_BUF_SIZE, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(err));
        return err;
    }
    
    // Configure UART parameters
    err = uart_param_config(GNSS_UART_NUM, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(err));
        uart_driver_delete(GNSS_UART_NUM);
        return err;
    }
    
    // Set UART pins
    err = uart_set_pin(GNSS_UART_NUM, GNSS_TX_PIN, GNSS_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(err));
        uart_driver_delete(GNSS_UART_NUM);
        return err;
    }
    
    ESP_LOGI(TAG, "UART2 initialized: %d baud, TX=GPIO%d, RX=GPIO%d", 
             GNSS_BAUD_RATE, GNSS_TX_PIN, GNSS_RX_PIN);
    
    return ESP_OK;
}

// GNSS Receiver Task
static void gnss_receiver_task(void *pvParameters) {
    char line_buffer[256];
    int line_pos = 0;
    TickType_t last_gga_time = xTaskGetTickCount() - pdMS_TO_TICKS(DEFAULT_GGA_INTERVAL_SEC * 1000);  // Force immediate send on first valid GGA
    uint16_t gga_interval_sec = DEFAULT_GGA_INTERVAL_SEC;
    
    ESP_LOGI(TAG, "GNSS Receiver Task started");
    
    // Initialize UART
    if (init_gnss_uart() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize GNSS UART, task exiting");
        vTaskDelete(NULL);
        return;
    }
    
    // Load GGA interval configuration
    ntrip_config_t ntrip_config;
    if (config_get_ntrip(&ntrip_config) == ESP_OK) {
        gga_interval_sec = ntrip_config.gga_interval_sec;
        ESP_LOGI(TAG, "GGA interval: %d seconds", gga_interval_sec);
    }
    
    while (1) {
        // Check for RTCM data from NTRIP Client
        rtcm_data_t rtcm_data;
        if (xQueueReceive(rtcm_queue, &rtcm_data, 0) == pdTRUE) {
            // Forward RTCM data to GPS receiver
            int written = uart_write_bytes(GNSS_UART_NUM, rtcm_data.data, rtcm_data.length);
            if (written < 0) {
                ESP_LOGW(TAG, "Failed to write RTCM data to GPS");
            } else {
                ESP_LOGD(TAG, "Forwarded %d bytes RTCM to GPS", written);
            }
        }
        
        // Read NMEA data from GPS receiver
        uint8_t data[128];
        int len = uart_read_bytes(GNSS_UART_NUM, data, sizeof(data) - 1, pdMS_TO_TICKS(GNSS_UART_TIMEOUT_MS));
        
        if (len > 0) {
            // Process received data byte by byte
            for (int i = 0; i < len; i++) {
                char c = data[i];
                
                // Start of new sentence
                if (c == '$') {
                    line_pos = 0;
                    line_buffer[line_pos++] = c;
                }
                // End of sentence
                else if (c == '\n' && line_pos > 0) {
                    line_buffer[line_pos] = '\0';
                    
                    // Process complete sentence
                    update_gnss_data(line_buffer);
                    
                    line_pos = 0;
                }
                // Build sentence
                else if (line_pos > 0 && line_pos < (int)sizeof(line_buffer) - 1) {
                    line_buffer[line_pos++] = c;
                }
                // Buffer overflow protection
                else if (line_pos >= (int)sizeof(line_buffer) - 1) {
                    ESP_LOGW(TAG, "Line buffer overflow, resetting");
                    line_pos = 0;
                }
            }
        }
        
        // Send GGA to NTRIP Client at configured interval
        TickType_t current_time = xTaskGetTickCount();
        if ((current_time - last_gga_time) >= pdMS_TO_TICKS(gga_interval_sec * 1000)) {
            if (xSemaphoreTake(gnss_data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                if (gnss_data.valid && strlen(gnss_data.gga) > 0) {
                    gga_data_t gga_data;
                    strncpy(gga_data.sentence, gnss_data.gga, sizeof(gga_data.sentence) - 1);
                    gga_data.sentence[sizeof(gga_data.sentence) - 1] = '\0';
                    
                    // Send to NTRIP Client (non-blocking)
                    if (xQueueSend(gga_queue, &gga_data, 0) == pdTRUE) {
                        ESP_LOGI(TAG, "Sent GGA to NTRIP queue: %s", gga_data.sentence);
                        last_gga_time = current_time;
                    } else {
                        ESP_LOGW(TAG, "GGA queue full, overwriting");
                        // For GGA queue, we want the latest data, so overwrite
                        xQueueReset(gga_queue);
                        xQueueSend(gga_queue, &gga_data, 0);
                        last_gga_time = current_time;
                    }
                } else {
                    ESP_LOGD(TAG, "GGA send interval elapsed but no valid GNSS data (valid=%d, gga_len=%d)", 
                             gnss_data.valid, strlen(gnss_data.gga));
                }
                xSemaphoreGive(gnss_data_mutex);
            }
        }
        
        // Check for configuration changes
        EventBits_t bits = config_wait_for_event(CONFIG_NTRIP_CHANGED_BIT, 0);
        if (bits & CONFIG_NTRIP_CHANGED_BIT) {
            // Reload GGA interval
            if (config_get_ntrip(&ntrip_config) == ESP_OK) {
                gga_interval_sec = ntrip_config.gga_interval_sec;
                ESP_LOGI(TAG, "GGA interval updated: %d seconds", gga_interval_sec);
            }
        }
        
        // Small delay to prevent busy-waiting
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Public API Implementation

void gnss_receiver_task_init(void) {
    // Create mutex for GNSS data
    if (gnss_data_mutex == NULL) {
        gnss_data_mutex = xSemaphoreCreateMutex();
        if (gnss_data_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create GNSS data mutex");
            return;
        }
    }
    
    // Create task
    BaseType_t result = xTaskCreate(
        gnss_receiver_task,
        "gnss_receiver",
        GNSS_TASK_STACK_SIZE,
        NULL,
        GNSS_TASK_PRIORITY,
        &gnss_task_handle
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create GNSS Receiver Task");
        if (gnss_data_mutex != NULL) {
            vSemaphoreDelete(gnss_data_mutex);
            gnss_data_mutex = NULL;
        }
    }
}

void gnss_get_data(gnss_data_t *data) {
    if (data == NULL) {
        return;
    }
    
    if (xSemaphoreTake(gnss_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(data, &gnss_data, sizeof(gnss_data_t));
        xSemaphoreGive(gnss_data_mutex);
    }
}

bool gnss_has_valid_fix(void) {
    bool valid = false;
    
    if (xSemaphoreTake(gnss_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Check if we have recent GGA data (within last 5 seconds)
        struct timeval tv;
        gettimeofday(&tv, NULL);
        
        valid = gnss_data.valid && 
                (tv.tv_sec - gnss_data.timestamp) < 5 &&
                strlen(gnss_data.gga) > 0;
        
        xSemaphoreGive(gnss_data_mutex);
    }
    
    return valid;
}

void gnss_receiver_task_stop(void) {
    if (gnss_task_handle != NULL) {
        vTaskDelete(gnss_task_handle);
        gnss_task_handle = NULL;
        ESP_LOGI(TAG, "GNSS Receiver Task stopped");
    }
    
    // Cleanup UART
    uart_driver_delete(GNSS_UART_NUM);
    
    if (gnss_data_mutex != NULL) {
        vSemaphoreDelete(gnss_data_mutex);
        gnss_data_mutex = NULL;
    }
}
