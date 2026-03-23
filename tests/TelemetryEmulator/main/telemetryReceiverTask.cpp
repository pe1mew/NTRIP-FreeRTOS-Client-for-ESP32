#include "telemetryReceiverTask.h"
#include "ledTask.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "CRC16.h"

#include <string.h>
#include <stdint.h>

/* ── Configuration ────────────────────────────────────────────────────────── */
#define RECV_UART_NUM        UART_NUM_1
#define RECV_RX_PIN          GPIO_NUM_5
#define RECV_TX_PIN          GPIO_NUM_4       /* defined, not wired */
#define RECV_BAUD_RATE       115200
#define RECV_BUF_SIZE        1024
#define RECV_FRAME_BUF_SIZE  256
#define RECV_TASK_STACK_SIZE 4096
#define RECV_TASK_PRIORITY   5

/* ── Framing constants ────────────────────────────────────────────────────── */
#define FRAME_SOH  0x01u
#define FRAME_CAN  0x18u
#define FRAME_DLE  0x10u

/* ── Statistics reporting interval ──────────────────────────────────────────*/
#define STATS_INTERVAL_FRAMES 100u

static const char *TAG = "TelemetryRx";

/* ── State machine ────────────────────────────────────────────────────────── */
typedef enum {
    STATE_WAIT_SOH,
    STATE_IN_FRAME,
    STATE_AFTER_DLE,
} frame_state_t;

/* ─────────────────────────────────────────────────────────────────────────── */
static void telemetry_receiver_task(void *arg)
{
    ESP_LOGI(TAG, "Telemetry Receiver Task started, listening on UART1 RX=GPIO5");

    /* Read buffer — raw bytes from UART ring buffer */
    uint8_t rx_buf[RECV_BUF_SIZE];
    /* Decoded frame accumulator (payload bytes + 2 CRC bytes after de-stuffing) */
    uint8_t frame_buf[RECV_FRAME_BUF_SIZE];
    int     frame_len = 0;

    frame_state_t state = STATE_WAIT_SOH;

    /* Statistics */
    uint32_t frames_total    = 0;
    uint32_t frames_ok       = 0;
    uint32_t frames_crc_err  = 0;
    uint32_t frames_overflow = 0;

    while (true) {
        int bytes = uart_read_bytes(RECV_UART_NUM, rx_buf, 1, portMAX_DELAY);
        if (bytes <= 0) {
            continue;
        }

        for (int i = 0; i < bytes; ++i) {
            uint8_t b = rx_buf[i];

            switch (state) {

            /* ── Waiting for start of frame ─────────────────────────────── */
            case STATE_WAIT_SOH:
                if (b == FRAME_SOH) {
                    frame_len = 0;
                    state = STATE_IN_FRAME;
                }
                /* Discard everything else */
                break;

            /* ── Accumulating frame bytes ────────────────────────────────── */
            case STATE_IN_FRAME:
                if (b == FRAME_SOH) {
                    /* Unexpected SOH mid-frame — restart */
                    ESP_LOGW(TAG, "Unexpected SOH mid-frame after %d bytes — restarting", frame_len);
                    frame_len = 0;
                    /* Stay in IN_FRAME state, new frame started */

                } else if (b == FRAME_DLE) {
                    state = STATE_AFTER_DLE;

                } else if (b == FRAME_CAN) {
                    /* ── End of frame received ─────────────────────────── */
                    ++frames_total;

                    if (frame_len < 2) {
                        /* Too short to contain even CRC bytes */
                        ESP_LOGW(TAG, "Frame [%u] too short (%d bytes) — discarding",
                                 (unsigned)frames_total, frame_len);
                        state = STATE_WAIT_SOH;
                        break;
                    }

                    /* Last two bytes in the buffer are the de-stuffed CRC */
                    uint8_t crc_h = frame_buf[frame_len - 2];
                    uint8_t crc_l = frame_buf[frame_len - 1];
                    uint16_t received_crc = (static_cast<uint16_t>(crc_h) << 8) | crc_l;

                    /* Payload is everything before the CRC bytes */
                    size_t payload_len = static_cast<size_t>(frame_len - 2);

                    /* Null-terminate so payload can be printed as a string */
                    frame_buf[payload_len] = '\0';

                    uint16_t computed_crc = calculateCRC16(frame_buf, payload_len);

                    if (computed_crc == received_crc) {
                        ++frames_ok;
                        led_blink_green();
                        ESP_LOGI(TAG, "Frame OK: %s",
                                 reinterpret_cast<const char *>(frame_buf));
                    } else {
                        ++frames_crc_err;
                        led_blink_red();
                        ESP_LOGE(TAG,
                                 "CRC FAIL: received=0x%04X computed=0x%04X payload='%s'",
                                 (unsigned)received_crc,
                                 (unsigned)computed_crc,
                                 reinterpret_cast<const char *>(frame_buf));
                    }

                    /* Periodic statistics */
                    if (frames_total % STATS_INTERVAL_FRAMES == 0) {
                        ESP_LOGI(TAG, "Stats: total=%u ok=%u crc_err=%u overflow=%u",
                                 (unsigned)frames_total,
                                 (unsigned)frames_ok,
                                 (unsigned)frames_crc_err,
                                 (unsigned)frames_overflow);
                    }

                    state = STATE_WAIT_SOH;

                } else {
                    /* Regular data byte — append to frame buffer */
                    if (frame_len >= RECV_FRAME_BUF_SIZE) {
                        ++frames_overflow;
                        ESP_LOGW(TAG, "Frame buffer overflow after %d bytes — discarding frame",
                                 frame_len);
                        state = STATE_WAIT_SOH;
                    } else {
                        frame_buf[frame_len++] = b;
                    }
                }
                break;

            /* ── DLE escape: next byte is literal ────────────────────────── */
            case STATE_AFTER_DLE:
                if (frame_len >= RECV_FRAME_BUF_SIZE) {
                    ++frames_overflow;
                    ESP_LOGW(TAG, "Frame buffer overflow (after DLE) after %d bytes — discarding frame",
                             frame_len);
                    state = STATE_WAIT_SOH;
                } else {
                    frame_buf[frame_len++] = b;
                    state = STATE_IN_FRAME;
                }
                break;
            }
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────────── */
esp_err_t telemetry_receiver_task_init(void)
{
    BaseType_t ret = xTaskCreate(
        telemetry_receiver_task,
        "telemetry_rx",
        RECV_TASK_STACK_SIZE,
        NULL,
        RECV_TASK_PRIORITY,
        NULL);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create telemetry receiver task");
        return ESP_FAIL;
    }
    return ESP_OK;
}
