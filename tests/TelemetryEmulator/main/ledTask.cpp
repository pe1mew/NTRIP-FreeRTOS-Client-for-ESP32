#include "ledTask.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "driver/gpio.h"
#include "esp_log.h"

/* ── Hardware ────────────────────────────────────────────────────────────────
 * LOLIN S3 onboard WS2812B RGB LED: GPIO38, GRB byte order.
 * RMT clock 40 MHz → 1 tick = 25 ns.  Same settings as ledIndicatorTask.
 *   T0H = 14 ticks (350 ns)   T0L = 36 ticks (900 ns)
 *   T1H = 36 ticks (900 ns)   T1L = 14 ticks (350 ns)
 * ─────────────────────────────────────────────────────────────────────────── */
#define LED_GPIO              GPIO_NUM_38
#define LED_RMT_RESOLUTION_HZ 40000000u   /* 40 MHz, 1 tick = 25 ns           */
#define LED_T0H               14u         /* WS2812 bit-0 high duration       */
#define LED_T0L               36u         /* WS2812 bit-0 low  duration       */
#define LED_T1H               36u         /* WS2812 bit-1 high duration       */
#define LED_T1L               14u         /* WS2812 bit-1 low  duration       */

#define LED_BRIGHTNESS        16u         /* 0-255; low to avoid eye strain   */
#define LED_BLINK_MS          100u        /* pulse duration per blink event   */
#define LED_POLL_MS           10u         /* LED update period                */

#define LED_TASK_STACK        2048
#define LED_TASK_PRIO         3

static const char *TAG = "LED";

/* ── Event type ─────────────────────────────────────────────────────────── */
typedef enum : uint8_t {
    LED_EVT_BLUE  = 0,
    LED_EVT_GREEN = 1,
    LED_EVT_RED   = 2,
} led_event_t;

/* ── Module state ────────────────────────────────────────────────────────── */
static QueueHandle_t         s_queue   = NULL;
static rmt_channel_handle_t  s_channel = NULL;
static rmt_encoder_handle_t  s_encoder = NULL;

/* ── RMT helper ──────────────────────────────────────────────────────────── */
static void set_led_color(uint8_t r, uint8_t g, uint8_t b)
{
    /* WS2812 expects bytes in GRB order */
    uint8_t grb[3] = { g, r, b };
    rmt_transmit_config_t tx_cfg = {};
    tx_cfg.loop_count = 0;
    rmt_transmit(s_channel, s_encoder, grb, sizeof(grb), &tx_cfg);
    rmt_tx_wait_all_done(s_channel, portMAX_DELAY);
}

/* ── LED task ────────────────────────────────────────────────────────────── */
static void led_task(void *arg)
{
    TickType_t blue_until  = 0;
    TickType_t green_until = 0;
    TickType_t red_until   = 0;
    const TickType_t blink_ticks = pdMS_TO_TICKS(LED_BLINK_MS);

    while (true) {
        led_event_t event;

        /* Block until an event arrives or the poll timeout expires */
        if (xQueueReceive(s_queue, &event, pdMS_TO_TICKS(LED_POLL_MS)) == pdTRUE) {
            TickType_t now = xTaskGetTickCount();
            switch (event) {
                case LED_EVT_BLUE:  blue_until  = now + blink_ticks; break;
                case LED_EVT_GREEN: green_until = now + blink_ticks; break;
                case LED_EVT_RED:   red_until   = now + blink_ticks; break;
            }
            /* Drain any additional pending events without blocking */
            while (xQueueReceive(s_queue, &event, 0) == pdTRUE) {
                now = xTaskGetTickCount();
                switch (event) {
                    case LED_EVT_BLUE:  blue_until  = now + blink_ticks; break;
                    case LED_EVT_GREEN: green_until = now + blink_ticks; break;
                    case LED_EVT_RED:   red_until   = now + blink_ticks; break;
                }
            }
        }

        /* Signed subtraction handles tick-counter wrap correctly */
        TickType_t now = xTaskGetTickCount();
        uint8_t r = ((int32_t)(red_until   - now) > 0) ? LED_BRIGHTNESS : 0u;
        uint8_t g = ((int32_t)(green_until - now) > 0) ? LED_BRIGHTNESS : 0u;
        uint8_t b = ((int32_t)(blue_until  - now) > 0) ? LED_BRIGHTNESS : 0u;

        set_led_color(r, g, b);
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */
esp_err_t led_task_init(void)
{
    /* ── RMT TX channel ───────────────────────────────────────────────────── */
    rmt_tx_channel_config_t tx_cfg = {};
    tx_cfg.gpio_num          = LED_GPIO;
    tx_cfg.clk_src           = RMT_CLK_SRC_DEFAULT;
    tx_cfg.resolution_hz     = LED_RMT_RESOLUTION_HZ;
    tx_cfg.mem_block_symbols = 64;
    tx_cfg.trans_queue_depth = 4;

    esp_err_t err = rmt_new_tx_channel(&tx_cfg, &s_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel: %s", esp_err_to_name(err));
        return err;
    }

    /* ── WS2812 bytes encoder ─────────────────────────────────────────────── */
    rmt_bytes_encoder_config_t enc_cfg = {};
    enc_cfg.bit0.level0     = 1;
    enc_cfg.bit0.duration0  = LED_T0H;
    enc_cfg.bit0.level1     = 0;
    enc_cfg.bit0.duration1  = LED_T0L;
    enc_cfg.bit1.level0     = 1;
    enc_cfg.bit1.duration0  = LED_T1H;
    enc_cfg.bit1.level1     = 0;
    enc_cfg.bit1.duration1  = LED_T1L;
    enc_cfg.flags.msb_first = 1;

    err = rmt_new_bytes_encoder(&enc_cfg, &s_encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_bytes_encoder: %s", esp_err_to_name(err));
        return err;
    }

    err = rmt_enable(s_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable: %s", esp_err_to_name(err));
        return err;
    }

    set_led_color(0, 0, 0);   /* ensure LED is off at startup */

    /* ── FreeRTOS queue and task ──────────────────────────────────────────── */
    s_queue = xQueueCreate(16, sizeof(led_event_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "Failed to create LED queue");
        return ESP_FAIL;
    }

    BaseType_t ret = xTaskCreate(led_task, "led_task", LED_TASK_STACK, NULL, LED_TASK_PRIO, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LED task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LED task started — WS2812 on GPIO38 (blue=TX, green=CRC OK, red=CRC FAIL)");
    return ESP_OK;
}

void led_blink_blue(void)
{
    if (!s_queue) return;
    led_event_t e = LED_EVT_BLUE;
    xQueueSend(s_queue, &e, 0);
}

void led_blink_green(void)
{
    if (!s_queue) return;
    led_event_t e = LED_EVT_GREEN;
    xQueueSend(s_queue, &e, 0);
}

void led_blink_red(void)
{
    if (!s_queue) return;
    led_event_t e = LED_EVT_RED;
    xQueueSend(s_queue, &e, 0);
}
