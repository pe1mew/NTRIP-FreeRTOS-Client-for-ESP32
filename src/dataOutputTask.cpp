/**
 * @file dataOutputTask.cpp
 * @brief Data Output Task implementation
 * 
 * This task formats and transmits telemetry data using a binary framing
 * protocol with byte stuffing and CRC-16 checksum for data integrity.
 * 
 * Protocol Format:
 * [SOH] [Message Data (stuffed)] [CRC-16 High (stuffed)] [CRC-16 Low (stuffed)] [CAN]
 * 
 * Message Format: YYYY-MM-DD HH:mm:ss.sss,LAT,LON,ALT,HEADING,SPEED
 * Example: 2026-01-10 14:30:52.123,-34.123456,150.987654,123.45,270.15,45.67
 */

#include "dataOutputTask.h"
#include "gnssReceiverTask.h"
#include "configurationManagerTask.h"
#include "hardware_config.h"
#include "lib/CRC16.h"
#include "statisticsTask.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "DataOutputTask";

// Task handle
static TaskHandle_t data_output_task_handle = NULL;

// Loop-time + transmit-count tracking (queried by stats task).
static int64_t dataout_prev_loop_us = 0;
static uint64_t dataout_loop_time_sum_us = 0;
static uint32_t dataout_loop_count = 0;
static uint32_t dataout_tx_count = 0;

extern "C" {
    TaskHandle_t data_output_task_get_handle(void) {
        return data_output_task_handle;
    }
    uint32_t data_output_task_get_avg_loop_us_and_reset(void) {
        uint32_t avg = (dataout_loop_count > 0)
            ? (uint32_t)(dataout_loop_time_sum_us / dataout_loop_count) : 0;
        dataout_loop_time_sum_us = 0;
        dataout_loop_count = 0;
        return avg;
    }
    // Returns number of successful UART transmissions since last call and resets.
    uint32_t data_output_get_tx_count_and_reset(void) {
        uint32_t n = dataout_tx_count;
        dataout_tx_count = 0;
        return n;
    }
}

// Queue for forwarding received telemetry JSON to the MQTT task
static QueueHandle_t s_json_queue = NULL;

// UART configuration
#define OUTPUT_UART_NUM         TELEMETRY_UART_NUM
#define OUTPUT_TX_PIN           TELEMETRY_TX_PIN
#define OUTPUT_RX_PIN           TELEMETRY_RX_PIN
#define OUTPUT_BAUD_RATE        115200
#define OUTPUT_BUF_SIZE         1024

/**
 * @brief Apply byte stuffing to data
 * 
 * If byte equals SOH, CAN, or DLE, prepend with DLE escape byte
 */
static size_t stuff_byte(uint8_t byte, uint8_t* output, size_t out_pos) {
    if (byte == FRAME_SOH || byte == FRAME_CAN || byte == FRAME_DLE) {
        output[out_pos++] = FRAME_DLE;
    }
    output[out_pos++] = byte;
    return out_pos;
}

/**
 * @brief Build framed telemetry message with CRC-16
 * 
 * @param pos Position data
 * @param frame Output buffer for framed message
 * @param frame_size Maximum frame buffer size
 * @return Length of framed message, or 0 on error
 */
static size_t build_telemetry_frame(const position_data_t* pos, uint8_t* frame, size_t frame_size) {
    if (!pos || !frame || frame_size < 256) {
        return 0;
    }

    // Format message string: YYYY-MM-DD HH:mm:ss.sss,LAT,LON,ALT,HEADING,SPEED,FIXQ
    char message[140];
    int msg_len = snprintf(message, sizeof(message),
                          "%04d-%02d-%02d %02d:%02d:%02d.%03d,%.6f,%.6f,%.2f,%.2f,%.2f,%u",
                          2000 + pos->year, pos->month, pos->day,
                          pos->hour, pos->minute, pos->second, pos->millisecond,
                          pos->latitude, pos->longitude,
                          pos->altitude, pos->heading, pos->speed,
                          pos->fix_quality);

    if (msg_len <= 0 || msg_len >= (int)sizeof(message)) {
        ESP_LOGE(TAG, "Failed to format message");
        return 0;
    }

    // Calculate CRC-16 over message string
    uint16_t crc = calculateCRC16((const uint8_t*)message, msg_len);
    uint8_t crc_high = (crc >> 8) & 0xFF;
    uint8_t crc_low = crc & 0xFF;

    // Build framed message with byte stuffing
    size_t pos_out = 0;

    // Start of frame
    frame[pos_out++] = FRAME_SOH;

    // Message data (with stuffing)
    for (int i = 0; i < msg_len; i++) {
        pos_out = stuff_byte((uint8_t)message[i], frame, pos_out);
    }

    // CRC-16 high byte (with stuffing)
    pos_out = stuff_byte(crc_high, frame, pos_out);

    // CRC-16 low byte (with stuffing)
    pos_out = stuff_byte(crc_low, frame, pos_out);

    // End of frame
    frame[pos_out++] = FRAME_CAN;

    return pos_out;
}

/**
 * @brief Receive and decode any pending UART1 RX frames, forwarding valid JSON to the queue.
 *
 * Uses a persistent state machine so partial frames are carried across calls.
 * Call once per task iteration (non-blocking — reads only bytes already in the UART FIFO).
 */
static void process_rx_frames(void) {
    // Receive state machine (static = persists across calls)
    typedef enum { WAIT_SOH, READING } rx_state_t;
    static rx_state_t state = WAIT_SOH;
    // Worst case: 512 payload bytes each stuffed → 1024 + 4 CRC bytes = ~1028 destuffed bytes.
    // We accumulate destuffed bytes so the buffer only needs payload + 2 CRC bytes.
    static uint8_t buf[TELEMETRY_JSON_MAX_LEN + 4];
    static size_t buf_len = 0;
    static bool dle_pending = false;

    uint8_t b;
    while (uart_read_bytes(OUTPUT_UART_NUM, &b, 1, 0) == 1) {
        if (state == WAIT_SOH) {
            if (b == FRAME_SOH) {
                buf_len = 0;
                dle_pending = false;
                state = READING;
            }
            // discard anything else
        } else { // READING
            if (dle_pending) {
                // This byte is a literal data value regardless of its value
                dle_pending = false;
                if (buf_len < sizeof(buf)) {
                    buf[buf_len++] = b;
                } else {
                    // Buffer overflow — discard frame
                    ESP_LOGW(TAG, "RX frame buffer overflow, discarding");
                    state = WAIT_SOH;
                }
            } else if (b == FRAME_DLE) {
                dle_pending = true;
            } else if (b == FRAME_CAN) {
                // End of frame — validate and forward
                // Need at least 3 bytes: 1 payload byte + 2 CRC bytes
                if (buf_len >= 3) {
                    size_t payload_len = buf_len - 2;
                    uint8_t crc_h = buf[payload_len];
                    uint8_t crc_l = buf[payload_len + 1];
                    uint16_t received_crc = ((uint16_t)crc_h << 8) | crc_l;
                    uint16_t calculated_crc = calculateCRC16(buf, payload_len);

                    if (calculated_crc == received_crc) {
                        // Valid CRC — ensure payload is a JSON object (starts with '{').
                        // This guards against looped-back CSV frames that share our framing
                        // format and whose CRC therefore also passes validation.
                        if (payload_len == 0 || buf[0] != '{') {
                            ESP_LOGD(TAG, "RX frame CRC ok but payload is not JSON (0x%02X), discarding", payload_len > 0 ? buf[0] : 0);
                        } else {
                        statistics_telemetry_received(true);
                        if (s_json_queue != NULL && payload_len < TELEMETRY_JSON_MAX_LEN) {
                            telemetry_json_msg_t msg;
                            memcpy(msg.json, buf, payload_len);
                            msg.json[payload_len] = '\0';
                            msg.len = payload_len;
                            if (xQueueSend(s_json_queue, &msg, 0) != pdTRUE) {
                                ESP_LOGD(TAG, "Telemetry JSON queue full, dropping frame");
                            }
                        }
                        }
                    } else {
                        ESP_LOGD(TAG, "RX frame CRC mismatch (got 0x%04X, expected 0x%04X)",
                                 received_crc, calculated_crc);
                        statistics_telemetry_received(false);
                    }
                } else {
                    ESP_LOGD(TAG, "RX frame too short (%d bytes), discarding", (int)buf_len);
                }
                state = WAIT_SOH;
            } else if (b == FRAME_SOH) {
                // Unexpected SOH — restart frame
                buf_len = 0;
                dle_pending = false;
            } else {
                if (buf_len < sizeof(buf)) {
                    buf[buf_len++] = b;
                } else {
                    ESP_LOGW(TAG, "RX frame buffer overflow, discarding");
                    state = WAIT_SOH;
                }
            }
        }
    }
}

/**
 * @brief Initialize UART1 for telemetry output
 */
static esp_err_t init_output_uart(void) {
    uart_config_t uart_config = {
        .baud_rate = OUTPUT_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {}
    };

    // Install UART driver
    esp_err_t err = uart_driver_install(OUTPUT_UART_NUM, OUTPUT_BUF_SIZE, OUTPUT_BUF_SIZE, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(err));
        return err;
    }

    // Configure UART parameters
    err = uart_param_config(OUTPUT_UART_NUM, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(err));
        uart_driver_delete(OUTPUT_UART_NUM);
        return err;
    }

    // Set UART pins
    err = uart_set_pin(OUTPUT_UART_NUM, OUTPUT_TX_PIN, OUTPUT_RX_PIN, 
                      UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(err));
        uart_driver_delete(OUTPUT_UART_NUM);
        return err;
    }

    ESP_LOGI(TAG, "UART1 initialized: %d baud, TX=GPIO%d, RX=GPIO%d",
             OUTPUT_BAUD_RATE, OUTPUT_TX_PIN, OUTPUT_RX_PIN);

    return ESP_OK;
}

/**
 * @brief Data Output Task main function
 */
static void data_output_task(void* pvParameters) {
    data_output_config_t config = {
        .interval_ms = DATA_OUTPUT_INTERVAL_MS,
        .enabled = true
    };

    ESP_LOGI(TAG, "Data Output Task started");

    // Initialize UART
    if (init_output_uart() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UART, task exiting");
        vTaskDelete(NULL);
        return;
    }

    uint8_t frame_buffer[256];
    position_data_t position;
    memset(&position, 0, sizeof(position_data_t));
    TickType_t last_output_time = xTaskGetTickCount();

    ESP_LOGI(TAG, "Waiting for GNSS data updates...");

    while (1) {
        // Sample loop period for the per-task avg-loop-time stat.
        {
            int64_t now_loop_us = esp_timer_get_time();
            if (dataout_prev_loop_us != 0) {
                dataout_loop_time_sum_us += (uint64_t)(now_loop_us - dataout_prev_loop_us);
                dataout_loop_count++;
            }
            dataout_prev_loop_us = now_loop_us;
        }

        // Check if output is enabled (future: read from configuration)
        if (!config.enabled) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // Wait for GNSS data update event or timeout at output interval
        xEventGroupWaitBits(
            gnss_event_group,
            GNSS_DATA_UPDATED_BIT,
            pdTRUE,  // Clear bit on exit
            pdFALSE, // Don't wait for all bits
            pdMS_TO_TICKS(config.interval_ms)
        );

        // Check if interval has elapsed (output at fixed rate)
        TickType_t current_time = xTaskGetTickCount();
        if ((current_time - last_output_time) < pdMS_TO_TICKS(config.interval_ms)) {
            continue; // Not time yet
        }
        last_output_time = current_time;

        // Get latest GNSS data (already parsed)
        gnss_data_t gnss_data;
        gnss_get_data(&gnss_data);

        // Copy parsed data directly to position structure
        position.valid = gnss_data.valid;
        position.latitude = gnss_data.latitude;
        position.longitude = gnss_data.longitude;
        position.altitude = gnss_data.altitude;
        position.heading = gnss_data.heading;
        position.speed = gnss_data.speed;
        position.day = gnss_data.day;
        position.month = gnss_data.month;
        position.year = gnss_data.year;
        position.hour = gnss_data.hour;
        position.minute = gnss_data.minute;
        position.second = gnss_data.second;
        position.millisecond = gnss_data.millisecond;
        position.fix_quality = gnss_data.fix_quality;

        // If no valid data, use default values
        if (!position.valid) {
            // Get current system time as fallback
            struct timeval tv;
            struct tm timeinfo;
            gettimeofday(&tv, NULL);
            localtime_r(&tv.tv_sec, &timeinfo);

            position.day = timeinfo.tm_mday;
            position.month = timeinfo.tm_mon + 1;
            position.year = timeinfo.tm_year % 100;
            position.hour = timeinfo.tm_hour;
            position.minute = timeinfo.tm_min;
            position.second = timeinfo.tm_sec;
            position.millisecond = tv.tv_usec / 1000;
            position.latitude = 0.0;
            position.longitude = 0.0;
            position.altitude = 0.0f;
            position.heading = 0.0f;
            position.speed = 0.0f;
        }

        // Build framed telemetry message
        size_t frame_len = build_telemetry_frame(&position, frame_buffer, sizeof(frame_buffer));

        if (frame_len > 0) {
            // Transmit frame via UART
            int written = uart_write_bytes(OUTPUT_UART_NUM, frame_buffer, frame_len);
            if (written < 0) {
                ESP_LOGW(TAG, "Failed to write telemetry data to UART");
                statistics_uart_error();
            } else {
                ESP_LOGD(TAG, "Transmitted %d bytes (valid=%d)", written, position.valid);
                dataout_tx_count++;  // for telemetry_output_rate_hz
            }
        } else {
            ESP_LOGW(TAG, "Failed to build telemetry frame");
        }

        // Process any incoming JSON frames from the telemetry unit on UART1 RX
        process_rx_frames();
    }
}

// Public API Implementation

QueueHandle_t data_output_get_json_queue(void) {
    return s_json_queue;
}

esp_err_t data_output_task_init(void) {
    ESP_LOGI(TAG, "Initializing Data Output Task");

    // Create JSON forwarding queue (depth 5, each item is a telemetry_json_msg_t)
    s_json_queue = xQueueCreate(5, sizeof(telemetry_json_msg_t));
    if (s_json_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create telemetry JSON queue");
        return ESP_FAIL;
    }

    // Create Data Output task
    BaseType_t result = xTaskCreate(
        data_output_task,
        "data_output",
        DATA_OUTPUT_TASK_STACK_SIZE,
        NULL,
        DATA_OUTPUT_TASK_PRIORITY,
        &data_output_task_handle
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Data Output Task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Data Output Task initialized successfully");
    return ESP_OK;
}

