#include "sensorEmulatorTask.h"
#include "ledTask.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "CRC16.h"

/* ── UART configuration ─────────────────────────────────────────────────── */
/* Shares UART1 with telemetryReceiverTask (RX=GPIO5 / TX=GPIO4).            */
/* The driver is installed and the pin is configured by app_main(); this      */
/* task only calls uart_write_bytes().                                        */
#define EMUL_UART_NUM        UART_NUM_1
#define EMUL_JSON_BUF_SIZE   384             /* max JSON payload length       */

/* ── Binary framing constants (identical to telemetryReceiverTask) ────────── */
#define FRAME_SOH  0x01u   /* Start of Header — never stuffed */
#define FRAME_CAN  0x18u   /* Cancel / end of frame — never stuffed */
#define FRAME_DLE  0x10u   /* Data Link Escape */

/* Worst-case wire frame size:
 *   1 (SOH) + 2*EMUL_JSON_BUF_SIZE (full payload stuffed) + 4 (stuffed CRC) + 1 (CAN)
 * In practice the JSON payload is pure printable ASCII so stuffing overhead
 * is zero for the payload; the two CRC bytes add at most 2 extra bytes. */
#define EMUL_WIRE_BUF_SIZE  (2 * EMUL_JSON_BUF_SIZE + 6)

/* ── Task parameters ─────────────────────────────────────────────────────── */
#define EMUL_TASK_STACK_SIZE   4096
#define EMUL_TASK_PRIORITY     4
/* Publish period is set via CONFIG_EMUL_PERIOD_MS in main/Kconfig.projbuild.
 * Default 1000 ms (1 Hz).  Override in sdkconfig or via idf.py menuconfig. */
#define EMUL_PERIOD_MS         ((uint32_t)CONFIG_EMUL_PERIOD_MS)

/* ── CRC-inject button ───────────────────────────────────────────────────── */
/* LOLIN S3 BOOT button — active LOW, internal pull-up enabled.              */
/* Hold while running to corrupt each outgoing frame and trigger CRC FAIL.   */
#define EMUL_CRC_INJECT_PIN    GPIO_NUM_0

static const char *TAG = "SensorEmul";

/* ═══════════════════════════════════════════════════════════════════════════
 * Deterministic waveform generators
 *
 * All functions accept:
 *   t       – time in seconds (float, always >= 0)
 *   period  – waveform period in seconds
 *   lo, hi  – output range [lo, hi]
 *
 * === Sine ===
 *   output = mid + amp * sin(2π·t / period)
 *   where  mid = (hi+lo)/2,  amp = (hi-lo)/2
 *
 * === Cosine ===
 *   Same as sine but using cos().
 *
 * === Triangle ===
 *   Phase φ = fmod(t, T) / T  ∈ [0,1)
 *   raw = (φ < 0.5) ? 4φ-1 : 3-4φ   →  raw ∈ [-1,+1]
 *   output = mid + amp * raw
 *
 * === Square ===
 *   output = (φ < 0.5) ? hi : lo
 *
 * === Trapezoid ===
 *   Symmetric: 25% rise │ 25% high │ 25% fall │ 25% low
 *   val ∈ [0,1] linearly interpolated per segment.
 *   output = lo + val*(hi-lo)
 *
 * === Rectified sine (|sin|) ===
 *   output = lo + |sin(2π·t/period)| * (hi-lo)
 *   Used for strictly non-negative signals whose physical minimum is 0.
 * ═══════════════════════════════════════════════════════════════════════════*/

static inline float wave_sine(float t, float period, float lo, float hi)
{
    const float mid = (hi + lo) * 0.5f;
    const float amp = (hi - lo) * 0.5f;
    return mid + amp * sinf(2.0f * (float)M_PI * t / period);
}

static inline float wave_cosine(float t, float period, float lo, float hi)
{
    const float mid = (hi + lo) * 0.5f;
    const float amp = (hi - lo) * 0.5f;
    return mid + amp * cosf(2.0f * (float)M_PI * t / period);
}

static inline float wave_triangle(float t, float period, float lo, float hi)
{
    const float mid  = (hi + lo) * 0.5f;
    const float amp  = (hi - lo) * 0.5f;
    const float phi  = fmodf(t, period) / period;  /* normalised phase [0,1) */
    const float raw  = (phi < 0.5f) ? (4.0f * phi - 1.0f)
                                    : (3.0f - 4.0f * phi);
    return mid + amp * raw;
}

static inline float wave_square(float t, float period, float lo, float hi)
{
    const float phi = fmodf(t, period) / period;
    return (phi < 0.5f) ? hi : lo;
}

static inline float wave_trapezoid(float t, float period, float lo, float hi)
{
    const float phi = fmodf(t, period) / period;  /* [0,1) */
    float val;
    if (phi < 0.25f) {
        val = phi / 0.25f;                         /* rise  0 → 1   */
    } else if (phi < 0.50f) {
        val = 1.0f;                                /* high  hold    */
    } else if (phi < 0.75f) {
        val = 1.0f - (phi - 0.50f) / 0.25f;       /* fall  1 → 0   */
    } else {
        val = 0.0f;                                /* low   hold    */
    }
    return lo + val * (hi - lo);
}

static inline float wave_abs_sine(float t, float period, float lo, float hi)
{
    return lo + fabsf(sinf(2.0f * (float)M_PI * t / period)) * (hi - lo);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Sensor Emulator Task
 *
 * Signal assignments
 * ──────────────────────────────────────────────────────────────────────────
 * JSON key │ Range               │ Waveform       │ Period
 * ─────────┼─────────────────────┼────────────────┼────────
 * accX     │ -4096 … +4095 cnt   │ sine           │  5 s
 * accY     │ -4096 … +4095 cnt   │ cosine         │  7 s
 * accZ     │ -4096 … +4095 cnt   │ triangle       │  3 s
 * thr      │ 0 … 100 %           │ trapezoid      │ 10 s
 * pwr      │ -1000 … +1000 W     │ sine           │  8 s
 * rpm      │ -10000 … +10000 RPM │ cosine         │ 12 s
 * trq      │ -100 … +100 Nm      │ square         │  6 s
 * vsc      │ 0.0 … 100.0 V       │ triangle       │ 15 s
 * tankP    │ 0.00 … 500.00 Bar   │ trapezoid      │ 20 s
 * fan      │ 0 … 100 %           │ |sine|         │ 10 s
 * h2P1     │ 0.00 … 1.00 Bar     │ sine           │  4 s
 * h2P2     │ 0.00 … 1.00 Bar     │ cosine         │  6 s
 * ═══════════════════════════════════════════════════════════════════════════*/
static void sensor_emulator_task(void *arg)
{
    ESP_LOGI(TAG, "Sensor Emulator Task started — binary framed output on UART1 TX=GPIO4 @ 115200");

    char    json_buf[EMUL_JSON_BUF_SIZE];  /* raw JSON payload (unstuffed) */
    uint8_t wire[EMUL_WIRE_BUF_SIZE];      /* fully framed wire bytes      */
    uint32_t seq = 0;

    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(EMUL_PERIOD_MS));

        /* ── Time base ────────────────────────────────────────────────────── */
        int64_t us = esp_timer_get_time();           /* µs since boot         */
        float   t  = (float)(us / 1000000LL)         /* whole seconds         */
                   + (float)(us % 1000000LL) * 1e-6f; /* fractional second    */

        /* Wall-clock timestamp (wraps at 24 h — fine for a test fixture) */
        uint32_t ms_total = (uint32_t)(us / 1000LL);
        uint32_t hh  = (ms_total / 3600000UL) % 24UL;
        uint32_t mm  = (ms_total /   60000UL) % 60UL;
        uint32_t ss  = (ms_total /    1000UL) % 60UL;
        uint32_t ms  =  ms_total %    1000UL;

        /* ── Vehicle block ────────────────────────────────────────────────── */
        /* ADXL345 in ±16 g full-resolution mode: 13-bit signed raw counts   */
        int16_t accX = (int16_t)wave_sine    (t,  5.0f, -4096.0f, 4095.0f);
        int16_t accY = (int16_t)wave_cosine  (t,  7.0f, -4096.0f, 4095.0f);
        int16_t accZ = (int16_t)wave_triangle(t,  3.0f, -4096.0f, 4095.0f);
        uint8_t thr  = (uint8_t)wave_trapezoid(t, 10.0f,    0.0f,  100.0f);

        /* ── Motor block ──────────────────────────────────────────────────── */
        int16_t pwr = (int16_t)wave_sine    (t,  8.0f, -1000.0f,  1000.0f);
        float   rpm =          wave_cosine  (t, 12.0f,-10000.0f, 10000.0f);
        int16_t trq = (int16_t)wave_square  (t,  6.0f,  -100.0f,   100.0f);

        /* ── Spectronik block ─────────────────────────────────────────────── */
        float   vsc   = wave_triangle (t, 15.0f,   0.0f,  100.0f);
        float   tankP = wave_trapezoid(t, 20.0f,   0.0f,  500.0f);
        uint8_t fan   = (uint8_t)wave_abs_sine(t, 10.0f,  0.0f,  100.0f);
        float   h2P1  = wave_sine    (t,  4.0f,   0.0f,    1.0f);
        float   h2P2  = wave_cosine  (t,  6.0f,   0.0f,    1.0f);

        /* ── Serialise JSON payload ──────────────────────────────────────── */
        int len = snprintf(json_buf, sizeof(json_buf),
            "{"
            "\"seq\":%u,"
            "\"tim\":\"%02u:%02u:%02u.%03u\","
            "\"vhl\":{\"accX\":%d,\"accY\":%d,\"accZ\":%d,\"thr\":%u},"
            "\"mtr\":{\"pwr\":%d,\"rpm\":%.1f,\"trq\":%d},"
            "\"spc\":{\"vsc\":%.1f,\"tankP\":%.2f,\"fan\":%u,"
                     "\"h2P1\":%.2f,\"h2P2\":%.2f}"
            "}",
            (unsigned)seq,
            (unsigned)hh, (unsigned)mm, (unsigned)ss, (unsigned)ms,
            (int)accX, (int)accY, (int)accZ, (unsigned)thr,
            (int)pwr, rpm, (int)trq,
            vsc, tankP, (unsigned)fan, h2P1, h2P2);

        if (len <= 0 || len >= (int)sizeof(json_buf)) {
            ESP_LOGE(TAG, "JSON buffer overflow at seq=%u (len=%d)", (unsigned)seq, len);
            ++seq;
            continue;
        }

        /* ── Compute CRC-16/CCITT-FALSE on raw payload ───────────────────── */
        uint16_t crc = calculateCRC16((const uint8_t *)json_buf, (size_t)len);

        /* ── CRC inject: corrupt payload if BOOT button is held ─────────── */
        if (gpio_get_level(EMUL_CRC_INJECT_PIN) == 0) {
            json_buf[0] ^= 0x01;   /* flip LSB of first byte — CRC will mismatch */
            ESP_LOGW(TAG, "CRC inject active — corrupting frame seq=%u", (unsigned)seq);
        }

        /* ── Build wire frame: SOH + stuffed payload + stuffed CRC + CAN ─── */
        int wlen = 0;

        /* 1. SOH — never stuffed */
        wire[wlen++] = FRAME_SOH;

        /* 2. Byte-stuffed payload                                            */
        /*    JSON is pure printable ASCII (≥ 0x20) so 0x01/0x10/0x18 cannot  */
        /*    appear in practice, but the loop handles them correctly anyway. */
        for (int i = 0; i < len; ++i) {
            uint8_t b = (uint8_t)json_buf[i];
            if (b == FRAME_SOH || b == FRAME_CAN || b == FRAME_DLE) {
                wire[wlen++] = FRAME_DLE;
            }
            wire[wlen++] = b;
        }

        /* 3. Stuffed CRC high byte */
        uint8_t crc_h = (uint8_t)(crc >> 8);
        if (crc_h == FRAME_SOH || crc_h == FRAME_CAN || crc_h == FRAME_DLE) {
            wire[wlen++] = FRAME_DLE;
        }
        wire[wlen++] = crc_h;

        /* 4. Stuffed CRC low byte */
        uint8_t crc_l = (uint8_t)(crc & 0xFFu);
        if (crc_l == FRAME_SOH || crc_l == FRAME_CAN || crc_l == FRAME_DLE) {
            wire[wlen++] = FRAME_DLE;
        }
        wire[wlen++] = crc_l;

        /* 5. CAN — never stuffed */
        wire[wlen++] = FRAME_CAN;

        uart_write_bytes(EMUL_UART_NUM, wire, (size_t)wlen);
        uart_wait_tx_done(EMUL_UART_NUM, portMAX_DELAY);
        led_blink_blue();
        ESP_LOGI(TAG, "seq=%u payload=%d wire=%d crc=0x%04X",
                 (unsigned)seq, len, wlen, (unsigned)crc);

        ++seq;
    }
}

/* ─────────────────────────────────────────────────────────────────────────── */
esp_err_t sensor_emulator_task_init(void)
{
    /* UART1 driver, baud rate, and pin assignment are handled by app_main().
     * Nothing to configure here except the CRC-inject button GPIO. */

    /* ── Configure BOOT button (GPIO0) as input with pull-up ─────────────── */
    gpio_config_t btn = {};
    btn.intr_type    = GPIO_INTR_DISABLE;
    btn.mode         = GPIO_MODE_INPUT;
    btn.pin_bit_mask = (1ULL << EMUL_CRC_INJECT_PIN);
    btn.pull_down_en = GPIO_PULLDOWN_DISABLE;
    btn.pull_up_en   = GPIO_PULLUP_ENABLE;
    gpio_config(&btn);

    /* ── Create task ──────────────────────────────────────────────────────── */
    BaseType_t ret = xTaskCreate(
        sensor_emulator_task,
        "sensor_emul",
        EMUL_TASK_STACK_SIZE,
        NULL,
        EMUL_TASK_PRIORITY,
        NULL);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sensor emulator task");
        return ESP_FAIL;
    }

    return ESP_OK;
}
