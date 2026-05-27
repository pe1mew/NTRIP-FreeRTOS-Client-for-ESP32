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
#include "Aggregator.h"
#include "telemetryReceiverTask.h"

/* ── UART configuration ─────────────────────────────────────────────────── */
/* Shares UART1 with telemetryReceiverTask (RX=GPIO5 / TX=GPIO4).            */
/* The driver is installed and the pin is configured by app_main(); this      */
/* task only calls uart_write_bytes().                                        */
#define EMUL_UART_NUM        UART_NUM_1
#define EMUL_JSON_BUF_SIZE   1024            /* max JSON payload length       */

#include "frame_protocol.h"

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
#define EMUL_SAMPLE_INTERVAL_MS  10u                               /* waveform sampling interval   */
#define EMUL_SAMPLE_TICKS        (EMUL_PERIOD_MS / EMUL_SAMPLE_INTERVAL_MS) /* ticks per publish */

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

/* Slow enum cycler — step through [0, n_values) with `step_seconds` per step.
 * Used for discrete state/mode fields (ctrl_mode, mtr_mode, sw_en, state,
 * dbg_mode) so they change visibly but predictably during testing. */
static inline int cycle_enum(float t, float step_seconds, int n_values)
{
    int step = (int)floorf(t / step_seconds);
    int v    = step % n_values;
    return (v < 0) ? v + n_values : v;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Sensor Emulator Task
 *
 * Samples all waveforms every EMUL_SAMPLE_INTERVAL_MS (10 ms) and feeds the
 * Aggregator instances.  At each EMUL_PERIOD_MS (default 1 s) publish tick
 * the accumulated min/max/avg snapshots are serialised into the JSON payload.
 *
 * The JSON mirrors the CSV protocol defined in
 * documentation/framedCRCString.md — ACC, MTL, MPW, THR are aggregated
 * (min/max/avg per field), SPC is last-received scalars, mode/state enums
 * carry the last-received value (slow cycler).
 *
 * Signal assignments
 * ──────────────────────────────────────────────────────────────────────────
 * JSON path             │ Range                  │ Waveform   │ Period
 * ──────────────────────┼────────────────────────┼────────────┼─────────
 * vhl.acc.x             │ -4096 … +4096 counts   │ sine       │   3 s
 * vhl.acc.y             │ -4096 … +4096 counts   │ cosine     │   3 s
 * vhl.acc.z             │  -512 … +4096 counts   │ triangle   │   5 s
 * vhl.thr.val           │ 0.0 … 100.0 %          │ trapezoid  │  60 s
 * vhl.thr.ctrlMode      │ 0 … 1                  │ cycle      │  30 s
 * vhl.thr.mtrMode       │ 0 … 7                  │ cycle      │  30 s
 * vhl.thr.swEn          │ 0 … 1                  │ cycle      │  60 s
 * vhl.thr.dbgMode       │ 0 … 1                  │ cycle      │  90 s
 * vhl.spd               │ 0.0 … 10.0 m/s         │ triangle   │ 120 s
 * vhl.lat, vhl.lon      │ signed decimal °       │ —          │  (NTRIP)
 * mtr.mtl.ctrl          │ -100.0 … +100.0        │ sine       │  20 s
 * mtr.mtl.ctrlMode      │ 0 … 1                  │ cycle      │  30 s
 * mtr.mtl.mtrMode       │ 0 … 7                  │ cycle      │  45 s
 * mtr.mtl.swEn          │ 0 … 1                  │ cycle      │  60 s
 * mtr.mtl.state         │ 0 … 3                  │ cycle      │  30 s
 * mtr.mtl.trq           │ -100 … +100 Nm         │ square     │   6 s
 * mtr.mtl.rpm           │ -10000 … +10000 RPM    │ cosine     │ 120 s
 * mtr.mtl.tmp           │ 25 … 85 °C             │ trapezoid  │ 300 s
 * mtr.mpw.pwr           │ -1000 … +1000 W        │ sine       │  60 s
 * mtr.mpw.cur           │ 0 … 200 A              │ triangle   │  15 s
 * spc.fan               │ 0 … 100 %              │ trapezoid  │  45 s
 * spc.h2P1              │ 0.30 … 0.70 bar        │ sine       │  10 s
 * spc.h2P2              │ 0.30 … 0.70 bar        │ cosine     │  10 s
 * spc.tankP             │ 0.00 … 500.00 bar      │ trapezoid  │ 240 s
 * spc.vsc               │ 0.0 … 100.0 V          │ triangle   │  15 s
 * spc.fsa               │ 0.0 … 50.0 A           │ trapezoid  │  12 s
 * ═══════════════════════════════════════════════════════════════════════════*/
static void sensor_emulator_task(void *arg)
{
    ESP_LOGI(TAG, "Sensor Emulator Task started — binary framed output on UART1 TX=GPIO4 @ 115200, sampled at %u ms", EMUL_SAMPLE_INTERVAL_MS);

    /* Large I/O buffers kept in BSS (static) — they would otherwise consume
     * ~3 KB of the 4 KB task stack and risk overflow once snprintf runs. */
    static char    json_buf[EMUL_JSON_BUF_SIZE];  /* raw JSON payload (unstuffed) */
    static uint8_t wire[EMUL_WIRE_BUF_SIZE];      /* fully framed wire bytes      */
    uint32_t seq = 0;

    /* Aggregators — one per published {min, max, avg} field */
    Aggregator agg_acc_x, agg_acc_y, agg_acc_z;     /* ACC: accelerometer */
    Aggregator agg_thr;                              /* THR: throttle control_value */
    Aggregator agg_spd;                              /* extra: vehicle speed */
    Aggregator agg_mtl_ctrl;                         /* MTL: control_value */
    Aggregator agg_mtl_trq;                          /* MTL: motor torque */
    Aggregator agg_mtl_rpm;                          /* MTL: motor RPM */
    Aggregator agg_mtl_tmp;                          /* MTL: motor temperature */
    Aggregator agg_mpw_pwr;                          /* MPW: motor power */
    Aggregator agg_mpw_cur;                          /* MPW: inverter peak current */
    uint32_t tick_count = 0;

    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(EMUL_SAMPLE_INTERVAL_MS));

        /*  Time base  */
        int64_t us = esp_timer_get_time();           /* µs since boot         */
        float   t  = (float)(us / 1000000LL)         /* whole seconds         */
                   + (float)(us % 1000000LL) * 1e-6f; /* fractional second    */

        /* Wall-clock timestamp (wraps at 24 h — fine for a test fixture) */
        uint32_t ms_total = (uint32_t)(us / 1000LL);
        uint32_t hh  = (ms_total / 3600000UL) % 24UL;
        uint32_t mm  = (ms_total /   60000UL) % 60UL;
        uint32_t ss  = (ms_total /    1000UL) % 60UL;
        uint32_t ms  =  ms_total %    1000UL;

        /* Sample aggregated waveforms — fed into Aggregators every 10 ms */
        agg_acc_x   .add(wave_sine     (t,    3.0f, -4096.0f,  4096.0f));
        agg_acc_y   .add(wave_cosine   (t,    3.0f, -4096.0f,  4096.0f));
        agg_acc_z   .add(wave_triangle (t,    5.0f,  -512.0f,  4096.0f));
        agg_thr     .add(wave_trapezoid(t,   60.0f,     0.0f,   100.0f));
        agg_spd     .add(wave_triangle (t,  120.0f,     0.0f,    10.0f));
        agg_mtl_ctrl.add(wave_sine     (t,   20.0f,  -100.0f,   100.0f));
        agg_mtl_trq .add(wave_square   (t,    6.0f,  -100.0f,   100.0f));
        agg_mtl_rpm .add(wave_cosine   (t,  120.0f,-10000.0f, 10000.0f));
        agg_mtl_tmp .add(wave_trapezoid(t,  300.0f,    25.0f,    85.0f));
        agg_mpw_pwr .add(wave_sine     (t,   60.0f, -1000.0f,  1000.0f));
        agg_mpw_cur .add(wave_triangle (t,   15.0f,     0.0f,   200.0f));

        ++tick_count;
        if (tick_count < EMUL_SAMPLE_TICKS) {
            continue;
        }
        tick_count = 0;

        /*  Publish tick  */

        /* Build timestamp string: use NTRIP wall clock if available, else boot-relative */
        NtripLatLon pos = telemetry_receiver_get_latlon();
        char boot_tim[13];
        const char *tim_str;
        if (pos.tim[0] != '\0') {
            tim_str = pos.tim;
        } else {
            snprintf(boot_tim, sizeof(boot_tim), "%02u:%02u:%02u.%03u",
                     (unsigned)hh, (unsigned)mm, (unsigned)ss, (unsigned)ms);
            tim_str = boot_tim;
        }

        /* Capture all aggregated windows */
        Aggregator::Snapshot s_acc_x    = agg_acc_x   .getSnapshot();
        Aggregator::Snapshot s_acc_y    = agg_acc_y   .getSnapshot();
        Aggregator::Snapshot s_acc_z    = agg_acc_z   .getSnapshot();
        Aggregator::Snapshot s_thr      = agg_thr     .getSnapshot();
        Aggregator::Snapshot s_spd      = agg_spd     .getSnapshot();
        Aggregator::Snapshot s_mtl_ctrl = agg_mtl_ctrl.getSnapshot();
        Aggregator::Snapshot s_mtl_trq  = agg_mtl_trq .getSnapshot();
        Aggregator::Snapshot s_mtl_rpm  = agg_mtl_rpm .getSnapshot();
        Aggregator::Snapshot s_mtl_tmp  = agg_mtl_tmp .getSnapshot();
        Aggregator::Snapshot s_mpw_pwr  = agg_mpw_pwr .getSnapshot();
        Aggregator::Snapshot s_mpw_cur  = agg_mpw_cur .getSnapshot();

        /* SPC fields are last-received scalars per CSV spec — sampled here */
        int   spc_fan   = (int)wave_trapezoid(t,  45.0f,   0.0f, 100.0f);
        float spc_h2P1  =      wave_sine     (t,  10.0f,   0.30f,  0.70f);
        float spc_h2P2  =      wave_cosine   (t,  10.0f,   0.30f,  0.70f);
        float spc_tankP =      wave_trapezoid(t, 240.0f,   0.0f, 500.0f);
        float spc_vsc   =      wave_triangle (t,  15.0f,   0.0f, 100.0f);
        float spc_fsa   =      wave_trapezoid(t,  12.0f,   0.0f,  50.0f);

        /* Discrete enum/state fields — slow cycle through valid values */
        int thr_ctrl_mode = cycle_enum(t, 30.0f, 2);
        int thr_mtr_mode  = cycle_enum(t, 30.0f, 8);
        int thr_sw_en     = cycle_enum(t, 60.0f, 2);
        int thr_dbg_mode  = cycle_enum(t, 90.0f, 2);
        int mtl_ctrl_mode = cycle_enum(t, 30.0f, 2);
        int mtl_mtr_mode  = cycle_enum(t, 45.0f, 8);
        int mtl_sw_en     = cycle_enum(t, 60.0f, 2);
        int mtl_state     = cycle_enum(t, 30.0f, 4);

        /* Serialise JSON payload — hybrid layout: vhl/mtr/spc envelope,
         * CSV-tag sub-blocks (acc, thr, mtl, mpw) per framedCRCString.md */
        int len = snprintf(json_buf, sizeof(json_buf),
            "{"
            "\"seq\":%u,"
            "\"tim\":\"%s\","
            "\"vhl\":{"
                "\"acc\":{"
                    "\"x\":{\"min\":%.2f,\"max\":%.2f,\"avg\":%.2f},"
                    "\"y\":{\"min\":%.2f,\"max\":%.2f,\"avg\":%.2f},"
                    "\"z\":{\"min\":%.2f,\"max\":%.2f,\"avg\":%.2f}"
                "},"
                "\"thr\":{"
                    "\"val\":{\"min\":%.1f,\"max\":%.1f,\"avg\":%.1f},"
                    "\"ctrlMode\":%d,\"mtrMode\":%d,\"swEn\":%d,\"dbgMode\":%d"
                "},"
                "\"spd\":{\"min\":%.2f,\"max\":%.2f,\"avg\":%.2f},"
                "\"lat\":%.7f,"
                "\"lon\":%.7f"
            "},"
            "\"mtr\":{"
                "\"mtl\":{"
                    "\"ctrl\":{\"min\":%.2f,\"max\":%.2f,\"avg\":%.2f},"
                    "\"ctrlMode\":%d,\"mtrMode\":%d,\"swEn\":%d,\"state\":%d,"
                    "\"trq\":{\"min\":%.1f,\"max\":%.1f,\"avg\":%.1f},"
                    "\"rpm\":{\"min\":%.1f,\"max\":%.1f,\"avg\":%.1f},"
                    "\"tmp\":{\"min\":%.1f,\"max\":%.1f,\"avg\":%.1f}"
                "},"
                "\"mpw\":{"
                    "\"pwr\":{\"min\":%.1f,\"max\":%.1f,\"avg\":%.1f},"
                    "\"cur\":{\"min\":%.2f,\"max\":%.2f,\"avg\":%.2f}"
                "}"
            "},"
            "\"spc\":{"
                "\"fan\":%d,"
                "\"h2P1\":%.2f,"
                "\"h2P2\":%.2f,"
                "\"tankP\":%.2f,"
                "\"vsc\":%.1f,"
                "\"fsa\":%.2f"
            "}"
            "}",
            (unsigned)seq,
            tim_str,
            /* vhl.acc.x   */ s_acc_x.min,   s_acc_x.max,   s_acc_x.avg,
            /* vhl.acc.y   */ s_acc_y.min,   s_acc_y.max,   s_acc_y.avg,
            /* vhl.acc.z   */ s_acc_z.min,   s_acc_z.max,   s_acc_z.avg,
            /* vhl.thr.val */ s_thr.min,     s_thr.max,     s_thr.avg,
            /* vhl.thr.*   */ thr_ctrl_mode, thr_mtr_mode, thr_sw_en, thr_dbg_mode,
            /* vhl.spd     */ s_spd.min,     s_spd.max,     s_spd.avg,
            /* vhl.lat/lon */ pos.lat,       pos.lon,
            /* mtr.mtl.ctrl */ s_mtl_ctrl.min, s_mtl_ctrl.max, s_mtl_ctrl.avg,
            /* mtr.mtl.*   */ mtl_ctrl_mode, mtl_mtr_mode, mtl_sw_en, mtl_state,
            /* mtr.mtl.trq */ s_mtl_trq.min,  s_mtl_trq.max,  s_mtl_trq.avg,
            /* mtr.mtl.rpm */ s_mtl_rpm.min,  s_mtl_rpm.max,  s_mtl_rpm.avg,
            /* mtr.mtl.tmp */ s_mtl_tmp.min,  s_mtl_tmp.max,  s_mtl_tmp.avg,
            /* mtr.mpw.pwr */ s_mpw_pwr.min,  s_mpw_pwr.max,  s_mpw_pwr.avg,
            /* mtr.mpw.cur */ s_mpw_cur.min,  s_mpw_cur.max,  s_mpw_cur.avg,
            /* spc.*       */ spc_fan, spc_h2P1, spc_h2P2, spc_tankP, spc_vsc, spc_fsa);

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
