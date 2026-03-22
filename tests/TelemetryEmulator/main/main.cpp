#include "freertos/FreeRTOS.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "telemetryReceiverTask.h"
#include "sensorEmulatorTask.h"
#include "ledTask.h"

static const char *TAG = "main";

/* ── UART1 configuration ────────────────────────────────────────────────────
 * Shared by both tasks:
 *   telemetryReceiverTask  – RX on GPIO5  (inbound binary frames)
 *   sensorEmulatorTask     – TX on GPIO4  (outbound JSON lines)
 * ─────────────────────────────────────────────────────────────────────────── */
#define RECV_UART_NUM   UART_NUM_1
#define RECV_RX_PIN     GPIO_NUM_5
#define RECV_TX_PIN     GPIO_NUM_4
#define RECV_BAUD_RATE  115200
#define RECV_BUF_SIZE   1024
#define RECV_TX_BUF     512

extern "C" void app_main(void)
{
    /* ── Configure and install UART1 (telemetry receiver) ─────────────────── */
    uart_config_t uart1_cfg;
    uart1_cfg.baud_rate           = RECV_BAUD_RATE;
    uart1_cfg.data_bits           = UART_DATA_8_BITS;
    uart1_cfg.parity              = UART_PARITY_DISABLE;
    uart1_cfg.stop_bits           = UART_STOP_BITS_1;
    uart1_cfg.flow_ctrl           = UART_HW_FLOWCTRL_DISABLE;
    uart1_cfg.rx_flow_ctrl_thresh = 0;
    uart1_cfg.source_clk          = UART_SCLK_DEFAULT;

    esp_err_t err = uart_driver_install(RECV_UART_NUM, RECV_BUF_SIZE, RECV_TX_BUF, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART1 driver install failed: %s", esp_err_to_name(err));
        abort();
    }

    err = uart_param_config(RECV_UART_NUM, &uart1_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART1 param config failed: %s", esp_err_to_name(err));
        abort();
    }

    /* TX=GPIO4 (sensorEmulatorTask), RX=GPIO5 (telemetryReceiverTask) */
    err = uart_set_pin(RECV_UART_NUM,
                       RECV_TX_PIN,        /* TX – GPIO4  */
                       RECV_RX_PIN,        /* RX – GPIO5  */
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART1 set_pin failed: %s", esp_err_to_name(err));
        abort();
    }

    /* ── Start LED indicator task ──────────────────────────────────────────── */
    err = led_task_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_task_init failed: %s", esp_err_to_name(err));
        abort();
    }

    /* ── Start telemetry receiver task ────────────────────────────────────── */
    err = telemetry_receiver_task_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "telemetry_receiver_task_init failed: %s", esp_err_to_name(err));
        abort();
    }

    /* ── Start sensor emulator task (JSON output on UART1 TX=GPIO4) ──────── */
    err = sensor_emulator_task_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sensor_emulator_task_init failed: %s", esp_err_to_name(err));
        abort();
    }

    /* FreeRTOS scheduler takes over; app_main may return */
}
