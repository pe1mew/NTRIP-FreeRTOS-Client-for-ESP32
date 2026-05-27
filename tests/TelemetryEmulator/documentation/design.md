# TelemetryEmulator — Design Document

**Module:** `tests/TelemetryEmulator`
**Version:** 4.2
**Date:** 2026-03-25
**Author:** NTRIP FreeRTOS Client Project

---

## 1. Purpose & Scope

The TelemetryEmulator is a standalone ESP32-S3 (LOLIN S3) firmware application that acts as a **bilateral test harness** for the NTRIP Client's telemetry and sensor-data pipelines. It runs two independent FreeRTOS tasks:

1. **`telemetryReceiverTask`** — acts as a **receiver** for binary telemetry frames transmitted by the NTRIP Client firmware (running on a second LOLIN S3 board) over a direct UART wire. The task decodes incoming frames, validates the CRC-16 checksum, and reports any errors to the debug console via `ESP_LOGE`. This validates one direction of the bilateral link: that the NTRIP Client correctly frames its outgoing position telemetry and that the CRC-16 is intact end-to-end.

2. **`sensorEmulatorTask`** — generates a deterministic JSON sensor-data packet every 1 second, wraps it in the SOH/DLE-stuffed/CRC-16/CAN binary frame format, and transmits it over UART1 TX (GPIO4) **to the NTRIP Client**. The NTRIP Client receives these frames, verifies their CRC-16, deserialises the JSON payload, and publishes the sensor fields to its configured MQTT broker. This validates the other direction: that the NTRIP Client correctly receives, parses, and forwards sensor data. Each JSON field is driven by a periodic waveform (sine, cosine, triangle, square, trapezoid, or rectified sine) scaled to the min/max range specified by the MQTT interface in `sensorData.md`. Because the output is protocol-compatible with `telemetryReceiverTask`, GPIO4 TX can also be wired to GPIO5 RX for a self-contained single-board loopback test that exercises the full encode → decode → CRC-validate chain without requiring the NTRIP Client board.

### Goals

- Receive binary telemetry frames from the NTRIP Client LOLIN S3 over a UART link
- Decode the binary framing protocol (byte de-stuffing, SOH/CAN boundary detection)
- Validate each frame's CRC-16 checksum and report failures via `ESP_LOGE`
- Log successfully decoded payloads via `ESP_LOGI` for visual confirmation
- Generate deterministic JSON sensor-data packets every 1 second with signal values driven by periodic waveforms (sine, cosine, triangle, square, trapezoid)
- Encode each packet as a binary frame (SOH + byte-stuffed payload + stuffed CRC-16 + CAN) and transmit it to the NTRIP Client over UART1 TX (GPIO4, 115200 baud)
- Verify that the NTRIP Client correctly receives, CRC-validates, and publishes the sensor data to its configured MQTT broker
- Act as a bilateral test harness: validate CRC of frames received **from** the NTRIP Client while simultaneously injecting test sensor data **to** the NTRIP Client
- Support a single-board loopback test by connecting GPIO4 TX → GPIO5 RX (exercises framing/CRC without the NTRIP Client board)
- Run both functions as independent FreeRTOS tasks, consistent with the architecture of the main project

### Non-Goals

- Does **not** act as an NTRIP client — it only processes telemetry frames and injects sensor data
- Does **not** run a WiFi stack, HTTP server, or MQTT client — the MQTT broker connection lives entirely within the NTRIP Client firmware
- Does **not** parse or validate the *values* of JSON fields it receives — it only validates frame framing and CRC-16 integrity
- Does **not** replace the host-side unit tests in `tests/NMEAparser/` or `tests/CRC16/`

---

## 2. Context

### 2.1 System Context

The `dataOutputTask` on the **NTRIP Client** ESP32-S3 transmits binary-framed position telemetry at 10 Hz over UART1 (115200 baud, TX on GPIO15). The TelemetryEmulator runs on a second LOLIN S3 and listens on its own UART RX pin. Both boards share a common GND. This arrangement allows the telemetry receiver logic to be tested independently of the GPS/NTRIP subsystem.

```
┌────────────────────────────────────────────┐
│  NTRIP Client — LOLIN S3 #1                │
│                                            │
│   dataOutputTask                           │
│   UART1 TX → GPIO15 ────────────────────┐  │
└────────────────────────────────────────────┘
                                           │  115200 baud 8N1
                    Physical wire          │
                                           ▼
┌─────────────────────────────────────────────────────┐
│  TelemetryEmulator — LOLIN S3 #2                    │
│                                                     │
│   GPIO5 RX ← UART1   telemetryReceiverTask          │
│                        → decode frame               │
│                        → validate CRC-16            │
│                        → ESP_LOGI / ESP_LOGE        │
│                                                     │
│   UART1 TX → GPIO4    sensorEmulatorTask            │
│                        → waveform-driven signals    │
│                        → JSON payload every 1 s     │
│                        → binary framed (SOH…CAN)    │
└─────────────────────────────────────────────────────┘

Shared: GND ──────────────────────────────────────────
```

### 2.2 Hardware Wiring

| Wire | From | To | Baud |
|------|------|----|------|
| Telemetry (Client → Emulator) | NTRIP Client GPIO15 (UART TX) | TelemetryEmulator GPIO5 (UART1 RX) | 115200 8N1 |
| Sensor data (Emulator → Client) | TelemetryEmulator GPIO4 (UART1 TX) | NTRIP Client GPIO\<n\> (UART RX — see NTRIP Client project) | 115200 8N1 |
| Ground | NTRIP Client GND | TelemetryEmulator GND | — |

> **Note:** The TelemetryEmulator uses a single UART peripheral (UART1) for both tasks. `telemetryReceiverTask` reads on the RX wire (GPIO5 ← NTRIP Client GPIO15) and `sensorEmulatorTask` writes on the TX wire (GPIO4 → NTRIP Client UART RX). The specific UART peripheral and GPIO pin used by the NTRIP Client for receiving sensor data is defined in the NTRIP Client project.

### 2.3 Relationship to Other Modules

```
tests/
├── catch2/           ← Catch2 v2 single-header (host-side tests only)
├── CRC16/            ← Host-side CRC-16 unit tests (Code::Blocks / MinGW)
├── NMEAparser/       ← Host-side NMEA unit tests (Code::Blocks / MinGW)
└── TelemetryEmulator/
    ├── documentation/
    │   ├── design.md         ← This document
    │   ├── json.md           ← JSON payload field reference and wire format
    │   └── sensorData.md     ← MQTT sensor data specification
    ├── CMakeLists.txt        ← ESP-IDF build
    ├── sdkconfig.defaults    ← Minimal ESP-IDF config
    ├── main/
    │   ├── CMakeLists.txt
    │   ├── main.cpp                  ← app_main()
    │   ├── frame_protocol.h          ← Shared framing constants (SOH, CAN, DLE)
    │   ├── ledTask.h
    │   ├── ledTask.cpp               ← WS2812B RGB LED indicator task
    │   ├── telemetryReceiverTask.h
    │   ├── telemetryReceiverTask.cpp
    │   ├── sensorEmulatorTask.h
    │   └── sensorEmulatorTask.cpp
    ├── tests/
    │   ├── aggregatorClass/  ← Host-side Aggregator unit tests (Code::Blocks / MinGW)
    │   │   ├── Aggregator.h          ← Header-only Aggregator class (authoritative copy)
    │   │   ├── main.cpp              ← Catch2 test cases (11 cases, 63 assertions)
    │   │   └── AggregatorTests.cbp   ← Code::Blocks project file
    │   └── waveform/         ← Host-side waveform unit tests (Code::Blocks / MinGW)
    │       ├── waveforms_standalone.h  ← Portable copy of wave_* functions
    │       ├── main.cpp                ← Catch2 test cases (6 cases, 736 assertions)
    │       └── WaveformTests.cbp       ← Code::Blocks project file
    └── components/
        └── crc16/            ← Shared CRC-16 (sourced from src/lib/CRC16)
            ├── CMakeLists.txt
            ├── CRC16.h
            └── CRC16.cpp
```

### 2.4 NTRIP Client — JSON Sensor Input and MQTT Forwarding

The NTRIP Client firmware includes a new function (provisionally named `sensorInputTask` or equivalent) that operates **in parallel with** the existing `dataOutputTask`. Its responsibilities are:

1. **Receive** binary-framed JSON sensor packets on a dedicated UART RX pin (same SOH / DLE-stuffed payload / stuffed CRC-16 / CAN protocol defined in §3).
2. **Validate** the CRC-16/CCITT-FALSE of each incoming frame; frames with a CRC mismatch are discarded and logged as errors.
3. **Deserialise** the JSON payload, extracting all sensor fields defined in `sensorData.md`.
4. **Publish** the sensor fields to the MQTT broker configured in the NTRIP Client (broker URL, port, topic, and credentials are `menuconfig` / `sdkconfig` settings in the NTRIP Client project — they are **not** part of the TelemetryEmulator firmware).

The TelemetryEmulator's `sensorEmulatorTask` is the intended source for those frames. During integration testing it continuously transmits well-formed packets at 1 Hz (or a faster rate configured via `CONFIG_EMUL_PERIOD_MS`), and optionally corrupted ones via the BOOT button, so both the happy path and the CRC-error path of the NTRIP Client's new function can be exercised.

> **Scope boundary:** Everything related to WiFi, MQTT broker connectivity, and topic naming is owned by the NTRIP Client project. This document only describes the wire interface and the frame format that the TelemetryEmulator presents to that function.

---

## 3. Protocol Specification

This section is normative. It mirrors exactly what `src/dataOutputTask.cpp` on the NTRIP Client implements. The TelemetryEmulator must decode this protocol faithfully.

### 3.1 Frame Structure

A single telemetry frame has the following wire format:

```
┌───────┬──────────────────────────────┬─────────────┬─────────────┬───────┐
│ SOH   │ Stuffed Message Payload      │ Stuffed     │ Stuffed     │ CAN   │
│ 0x01  │ (ASCII CSV, variable length) │ CRC-H       │ CRC-L       │ 0x18  │
└───────┴──────────────────────────────┴─────────────┴─────────────┴───────┘
  1 byte         N bytes (variable)          1–2 bytes   1–2 bytes   1 byte
```

| Field | Value | Notes |
|-------|-------|-------|
| SOH | `0x01` | Start of frame — **never** byte-stuffed |
| Stuffed Payload | variable | Message bytes after byte stuffing applied |
| Stuffed CRC-H | 1–2 bytes | High byte of CRC-16 after stuffing |
| Stuffed CRC-L | 1–2 bytes | Low byte of CRC-16 after stuffing |
| CAN | `0x18` | End of frame — **never** byte-stuffed |

### 3.2 Byte De-stuffing

**Transmitter (NTRIP Client):** If a payload or CRC byte equals `SOH`, `CAN`, or `DLE`, the transmitter inserts a `DLE` escape byte immediately before it.

**Receiver (TelemetryEmulator):** When a `DLE` byte is encountered, the next byte is the literal data byte (strip the `DLE`, keep what follows).

```
Decoding rule:
  b = next_byte()
  if b == DLE:
      data_byte = next_byte()   // literal, not a control byte
  else:
      data_byte = b
```

| Constant | Value | Meaning |
|----------|-------|---------|
| `FRAME_SOH` | `0x01` | Start of Header (Control-A) |
| `FRAME_CAN` | `0x18` | Cancel / end of frame (Control-X) |
| `FRAME_DLE` | `0x10` | Data Link Escape |

### 3.3 Message Payload Format

The payload is a UTF-8/ASCII CSV string with **no trailing newline**:

```
YYYY-MM-DD HH:mm:ss.sss,LAT,LON,ALT,HEADING,SPEED,FIXQ
```

| Field | Format | Example | Description |
|-------|--------|---------|-------------|
| Date | `YYYY-MM-DD` | `2026-03-22` | Calendar date |
| Time | `HH:mm:ss.sss` | `14:30:52.123` | UTC time with milliseconds |
| LAT | `%.6f` | `-34.123456` | Latitude in decimal degrees (signed) |
| LON | `%.6f` | `150.987654` | Longitude in decimal degrees (signed) |
| ALT | `%.2f` | `123.45` | Altitude in meters above ellipsoid |
| HEADING | `%.2f` | `270.15` | True heading in degrees (0–359.99) |
| SPEED | `%.2f` | `45.67` | Ground speed in km/h |
| FIXQ | `%u` | `4` | GNSS fix quality (see table below) |

**Fix Quality Values:**

| Value | Meaning |
|-------|---------|
| `0` | No fix |
| `1` | GPS (standalone) |
| `2` | DGPS |
| `4` | RTK Fixed |
| `5` | RTK Float |

**Full example payload:**
```
2026-03-22 14:30:52.123,-34.123456,150.987654,123.45,270.15,45.67,4
```

### 3.4 CRC-16 Checksum

- **Algorithm:** CRC-16-CCITT-FALSE (XMODEM variant)
- **Polynomial:** `0x1021`, initial value `0xFFFF`
- **Input:** De-stuffed payload bytes **only** (CRC bytes are not included in the CRC computation)
- **Endianness on wire:** High byte first, then low byte, both byte-stuffed independently

**Receiver validation:**
1. Accumulate all de-stuffed payload bytes.
2. After reading the stuffed CRC-H and CRC-L bytes, de-stuff them to obtain `received_crc`.
3. Compute `computed_crc = calculateCRC16(payload, payload_len)`.
4. If `computed_crc != received_crc` → CRC error.

---

## 4. Architecture

### 4.1 Component Overview

| Component | File(s) | Responsibility |
|-----------|---------|----------------|
| **Application entry** | `main/main.cpp` | `app_main()`: UART1 init (RX=GPIO5, TX=GPIO4, with TX ring buffer), then starts LED, receiver, and emulator tasks |
| **LED indicator task** | `main/ledTask.h/.cpp` | FreeRTOS task driven by a queue; blinks the onboard WS2812B RGB LED: green (CRC OK), red (CRC FAIL), blue (packet sent) |
| **Receiver task** | `main/telemetryReceiverTask.h/.cpp` | FreeRTOS task, frame state machine, CRC validation, `ESP_LOG` reporting — uses UART1 RX |
| **Sensor emulator task** | `main/sensorEmulatorTask.h/.cpp` | FreeRTOS task, deterministic waveform generation, JSON serialisation, UART1 TX output at 1 Hz; BOOT button (GPIO0) triggers CRC injection |
| **CRC-16** | `components/crc16/CRC16.h/.cpp` | Shared CRC-16-CCITT-FALSE calculation (sourced from `src/lib/CRC16`) |

### 4.2 Component Diagram (PlantUML)

![Component Diagram](Component_Diagram.png)

```plantuml
@startuml TelemetryEmulator_Components

skinparam component {
    BackgroundColor<<task>>     #LightBlue
    BackgroundColor<<protocol>> #LightGreen
    BackgroundColor<<io>>       #LightCoral
    BackgroundColor<<util>>     #Wheat
    BackgroundColor<<external>> #Plum
}

package "TelemetryEmulator Firmware — LOLIN S3 #2" {

    component [app_main()\ninitialise UART1 (RX=GPIO5, TX=GPIO4)\ncreate tasks] as Main <<task>>

    component [ledTask\n(FreeRTOS task)] as LedTask <<task>>
    note right of LedTask
      Queue-driven WS2812B
      RGB LED (GPIO38):
        green  = CRC OK
        red    = CRC FAIL
        blue   = packet sent
    end note

    component [telemetryReceiverTask\n(FreeRTOS task)] as RecvTask <<task>>
    note right of RecvTask
      State machine:
        WAIT_SOH
        IN_FRAME
        AFTER_DLE
      On frame complete:
        de-stuff payload
        validate CRC-16
        led_blink_green/red()
        ESP_LOGI / ESP_LOGE
    end note

    component [sensorEmulatorTask\n(FreeRTOS task)] as EmulTask <<task>>
    note left of EmulTask
      1 Hz timer (vTaskDelayUntil)
      waveform generators:
        sine, cosine, triangle
        square, trapezoid, |sine|
      JSON serialise
      CRC-16 compute
      BOOT button CRC-inject
      byte-stuff + frame
      uart_write_bytes(UART1 TX)
      uart_wait_tx_done()
      led_blink_blue()
    end note

    component [CRC-16\n(calculateCRC16)] as CRC16 <<util>>

    component [ESP-IDF UART1 Driver\n(115200 8N1\nRX=GPIO5, TX=GPIO4)] as UART1 <<io>>

    component [GPIO0\n(BOOT button\nINPUT_PULLUP)] as BOOTBTN <<io>>

    Main --> LedTask   : xTaskCreate()
    Main --> RecvTask  : xTaskCreate()
    Main --> EmulTask  : xTaskCreate()
    RecvTask --> UART1  : uart_read_bytes(1 byte)
    RecvTask --> CRC16  : calculateCRC16(payload, len)
    RecvTask --> LedTask : led_blink_green() / led_blink_red()
    RecvTask --> RecvTask : ESP_LOGI / ESP_LOGE
    EmulTask --> BOOTBTN : gpio_get_level()
    EmulTask --> UART1  : uart_write_bytes() + uart_wait_tx_done()
    EmulTask --> LedTask : led_blink_blue()
    EmulTask --> EmulTask : ESP_LOGI / ESP_LOGW
    EmulTask --> CRC16  : calculateCRC16(payload, len)
}

component [NTRIP Client LOLIN S3\n(dataOutputTask)] as NTRIPBoard <<external>>
component [USB Debug Monitor] as Consumer <<external>>

NTRIPBoard --> UART1 : GPIO15 TX → GPIO5 RX\n115200 baud 8N1
Main --> Consumer : UART0 (USB)\nESP_LOG output

@enduml
```

### 4.3 Frame Decoder State Machine

The receiver task processes incoming bytes one at a time using a three-state machine:

![Frame Decoder State Machine](FrameDecoderStateMachine.png)

```plantuml
@startuml TelemetryEmulator_StateMachine

[*] --> WAIT_SOH

WAIT_SOH --> WAIT_SOH   : byte ≠ SOH\n(discard)
WAIT_SOH --> IN_FRAME   : byte == SOH\n(reset buffer)

IN_FRAME --> WAIT_SOH   : byte == SOH\n(unexpected SOH:\nreset, restart)
IN_FRAME --> AFTER_DLE  : byte == DLE\n(next byte is literal)
IN_FRAME --> WAIT_SOH   : byte == CAN\n[frame complete]\n→ validate CRC\n→ log result
IN_FRAME --> IN_FRAME   : any other byte\n(append to buffer)

AFTER_DLE --> IN_FRAME  : any byte\n(append literal to buffer)

@enduml
```

**Buffer notes:**
- Maximum frame buffer: 512 bytes (`RECV_FRAME_BUF_SIZE` — must be ≥ `EMUL_JSON_BUF_SIZE` + 2 CRC bytes)
- On buffer overflow before `CAN`: discard frame, log `ESP_LOGW`, return to `WAIT_SOH`
- The CRC bytes are the **last two bytes** appended before `CAN`; all preceding bytes are payload

### 4.4 Sequence Diagram

![Sequence Diagram](SequenceDiagram.png)

```plantuml
@startuml TelemetryEmulator_Sequence

participant "NTRIP Client\n(dataOutputTask)" as Sender
participant "UART1 HW" as UART
participant "telemetryReceiverTask" as Task
participant "CRC-16\ncalculateCRC16()" as CRC

Sender -> UART : SOH + stuffed_payload\n+ stuffed_CRC_H\n+ stuffed_CRC_L + CAN

loop forever
    Task -> UART : uart_read_bytes(buf, 1, portMAX_DELAY)
    UART --> Task : 1 byte

    loop for each byte
        alt byte == SOH
            Task -> Task : state = IN_FRAME\nreset buffer
        else byte == CAN  (and state == IN_FRAME)
            Task -> Task : split buffer →\npayload[0..n-3]\nreceived_crc_h = buf[n-2]\nreceived_crc_l = buf[n-1]
            Task -> CRC  : calculateCRC16(payload, payload_len)
            CRC --> Task : computed_crc

            alt computed_crc == received_crc
                Task -> Task : ESP_LOGI(TAG,\n"Frame OK: %s", payload)
            else
                Task -> Task : ESP_LOGE(TAG,\n"CRC FAIL: got 0x%04X expect 0x%04X",\nreceived_crc, computed_crc)
            end

            Task -> Task : state = WAIT_SOH
        else byte == DLE  (and state == IN_FRAME)
            Task -> Task : state = AFTER_DLE
        else (any other byte, or AFTER_DLE)
            Task -> Task : append to buffer\nstate = IN_FRAME
        end
    end
end

@enduml
```

---

## 5. Module Definitions

### 5.1 `app_main()` — `main/main.cpp`

Responsibilities:
1. Configure and install UART1 (`UART_NUM_1`, 115200 baud, 8N1, RX=GPIO5, TX=GPIO4) with a 1024-byte RX ring buffer and a 512-byte TX ring buffer
2. Call `led_task_init()` to create the LED indicator FreeRTOS task (configures RMT for the onboard WS2812B on GPIO38)
3. Call `telemetry_receiver_task_init()` to create the telemetry receiver FreeRTOS task
4. Call `sensor_emulator_task_init()` to create the sensor emulator FreeRTOS task (configures GPIO0 as input with pull-up, then creates the task; reuses UART1)
5. Return (the task scheduler takes over)

### 5.2 `telemetryReceiverTask` — `main/telemetryReceiverTask.h/.cpp`

**Public API:**

```c
/**
 * @brief Initialize and start the Telemetry Receiver Task.
 *
 * Assumes UART has already been configured by app_main().
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t telemetry_receiver_task_init(void);
```

**Internal behaviour:**

```c
#define RECV_UART_NUM        UART_NUM_1
#define RECV_RX_PIN          GPIO_NUM_5
#define RECV_TX_PIN          GPIO_NUM_4      /* defined, not wired */
#define RECV_BAUD_RATE       115200
#define RECV_BUF_SIZE        1024
#define RECV_FRAME_BUF_SIZE  512  /* must be >= EMUL_JSON_BUF_SIZE + 2 CRC bytes */
#define RECV_TASK_STACK_SIZE 4096
#define RECV_TASK_PRIORITY   5

#include "frame_protocol.h"  /* FRAME_SOH 0x01 / FRAME_CAN 0x18 / FRAME_DLE 0x10 */

typedef enum {
    STATE_WAIT_SOH,
    STATE_IN_FRAME,
    STATE_AFTER_DLE,
} frame_state_t;
```

**Key counters tracked per task run (logged periodically):**

| Counter | Description |
|---------|-------------|
| `frames_total` | Total complete frames received |
| `frames_ok` | Frames with valid CRC |
| `frames_crc_error` | Frames with CRC mismatch |
| `frames_overflow` | Frames discarded due to buffer overflow |

Statistics are logged via `ESP_LOGI` every 100 frames.

### 5.3 `sensorEmulatorTask` — `main/sensorEmulatorTask.h/.cpp`

**Public API:**

```c
/**
 * @brief Initialise and start the Sensor Emulator Task.
 *
 * Assumes UART1 has already been configured by app_main() with TX=GPIO4.
 * Transmits one binary-framed JSON telemetry packet per second over
 * UART1 TX (GPIO4, 115200 8N1).  Frame format is identical to the
 * protocol decoded by telemetryReceiverTask (SOH + stuffed payload +
 * stuffed CRC-16/CCITT-FALSE + CAN).
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t sensor_emulator_task_init(void);
```

**Internal configuration:**

```c
#define EMUL_UART_NUM        UART_NUM_1   /* shared with telemetryReceiverTask */
#define EMUL_JSON_BUF_SIZE   512          /* max JSON payload length            */
#define EMUL_WIRE_BUF_SIZE   (2 * EMUL_JSON_BUF_SIZE + 6)  /* worst-case frame */
#define EMUL_TASK_STACK_SIZE   4096
#define EMUL_TASK_PRIORITY     4
#define EMUL_PERIOD_MS         ((uint32_t)CONFIG_EMUL_PERIOD_MS)  /* Kconfig, default 1000 ms */

/* CRC-inject button — LOLIN S3 BOOT button, active LOW, internal pull-up */
#define EMUL_CRC_INJECT_PIN    GPIO_NUM_0

#include "frame_protocol.h"  /* FRAME_SOH 0x01 / FRAME_CAN 0x18 / FRAME_DLE 0x10 */
```

> UART1 driver installation, baud rate, and pin assignment (TX=GPIO4) are performed exclusively by `app_main()`. `sensor_emulator_task_init()` configures GPIO0 as an input with pull-up and then creates the FreeRTOS task.

**CRC injection (BOOT button):** After the CRC is computed over the clean payload but before the wire frame is built, the task reads GPIO0. If the BOOT button is held (pin LOW), bit 0 of the first payload byte is flipped. The CRC in the frame remains correct for the *original* payload bytes, so the receiver's recomputed CRC will not match → `CRC FAIL`. Releasing the button restores clean frames immediately. A `WARN`-level log is emitted for every corrupted frame.

**Sender log timing:** `uart_wait_tx_done()` is called after `uart_write_bytes()` so the `SensorEmul` log line appears only after the last bit of the frame has cleared the UART shift register. This ensures the log timestamp reflects the actual end-of-transmission rather than the moment bytes were queued in the TX ring buffer.

**Wire frame produced per packet:**

```
┌───────┬──────────────────────────────┬─────────────┬─────────────┬───────┐
│ SOH   │ Stuffed JSON Payload         │ Stuffed     │ Stuffed     │ CAN   │
│ 0x01  │ (ASCII, variable length)     │ CRC-H       │ CRC-L       │ 0x18  │
└───────┴──────────────────────────────┴─────────────┴─────────────┴───────┘
```

The byte-stuffing and CRC-16/CCITT-FALSE rules are **identical** to the protocol decoded by `telemetryReceiverTask` (see §3). The JSON payload is pure printable ASCII so control bytes (`0x01`, `0x10`, `0x18`) cannot appear in the payload in practice, but the stuffing loop handles them correctly regardless. The two CRC bytes are stuffed independently.

**JSON output schema** (see `documentation/json.md` for the full field reference;
mirrors the CSV payload tags in `documentation/framedCRCString.md`):

```json
{"seq":N,"tim":"HH:MM:SS.mmm",
 "vhl":{
   "acc":{"x":{min,max,avg},"y":{min,max,avg},"z":{min,max,avg}},
   "thr":{"val":{min,max,avg},
          "ctrlMode":I,"mtrMode":I,"swEn":I,"dbgMode":I},
   "spd":{min,max,avg},"lat":F,"lon":F},
 "mtr":{
   "mtl":{"ctrl":{min,max,avg},
          "ctrlMode":I,"mtrMode":I,"swEn":I,"state":I,
          "trq":{min,max,avg},"rpm":{min,max,avg},"tmp":{min,max,avg}},
   "mpw":{"pwr":{min,max,avg},"cur":{min,max,avg}}},
 "spc":{"fan":I,"h2P1":F,"h2P2":F,"tankP":F,"vsc":F,"fsa":F}}
```

`lat`/`lon` are scalars sourced from the most recent NTRIP-Client frame
decoded by `telemetryReceiverTask`. All `spc.*` fields are last-received
scalars (per CSV-protocol §8.6 — Spectronik data is not aggregated). Discrete
enum/state fields (`ctrlMode`, `mtrMode`, `swEn`, `state`, `dbgMode`) are
last-received values driven by a slow cycler. Every `{min, max, avg}` aggregate
is computed by Welford's algorithm over the publish window.

**Waveform assignments:**

| JSON path | Range | Waveform | Period | Published as |
|-----------|-------|----------|--------|--------------|
| `vhl.acc.x` | −4096 … +4096 counts | sine |   3 s | `{min, max, avg}` |
| `vhl.acc.y` | −4096 … +4096 counts | cosine |   3 s | `{min, max, avg}` |
| `vhl.acc.z` |  −512 … +4096 counts | triangle |   5 s | `{min, max, avg}` |
| `vhl.thr.val` | 0.0 … 100.0 % | trapezoid |  60 s | `{min, max, avg}` |
| `vhl.thr.ctrlMode` | 0 … 1 | cycle |  30 s | scalar (last) |
| `vhl.thr.mtrMode`  | 0 … 7 | cycle |  30 s | scalar (last) |
| `vhl.thr.swEn`     | 0 … 1 | cycle |  60 s | scalar (last) |
| `vhl.thr.dbgMode`  | 0 … 1 | cycle |  90 s | scalar (last) |
| `vhl.spd` | 0.0 … 10.0 m/s | triangle | 120 s | `{min, max, avg}` |
| `vhl.lat` / `vhl.lon` | signed decimal ° | — | — | scalar (NTRIP receiver) |
| `mtr.mtl.ctrl` | −100.0 … +100.0 | sine |  20 s | `{min, max, avg}` |
| `mtr.mtl.ctrlMode` | 0 … 1 | cycle |  30 s | scalar (last) |
| `mtr.mtl.mtrMode`  | 0 … 7 | cycle |  45 s | scalar (last) |
| `mtr.mtl.swEn`     | 0 … 1 | cycle |  60 s | scalar (last) |
| `mtr.mtl.state`    | 0 … 3 | cycle |  30 s | scalar (last) |
| `mtr.mtl.trq` | −100 … +100 Nm | square |   6 s | `{min, max, avg}` |
| `mtr.mtl.rpm` | −10000 … +10000 RPM | cosine | 120 s | `{min, max, avg}` |
| `mtr.mtl.tmp` | 25 … 85 °C | trapezoid | 300 s | `{min, max, avg}` |
| `mtr.mpw.pwr` | −1000 … +1000 W | sine |  60 s | `{min, max, avg}` |
| `mtr.mpw.cur` | 0 … 200 A | triangle |  15 s | `{min, max, avg}` |
| `spc.fan` | 0 … 100 % | trapezoid |  45 s | scalar (last) |
| `spc.h2P1` | 0.30 … 0.70 bar | sine |  10 s | scalar (last) |
| `spc.h2P2` | 0.30 … 0.70 bar | cosine |  10 s | scalar (last) |
| `spc.tankP` | 0.00 … 500.00 bar | trapezoid | 240 s | scalar (last) |
| `spc.vsc` | 0.0 … 100.0 V | triangle |  15 s | scalar (last) |
| `spc.fsa` | 0.0 … 50.0 A | trapezoid |  12 s | scalar (last) |

**Waveform formulae** (continuous-time, all periods independent):

| Waveform | Formula |
|----------|---------|
| Sine | `mid + amp·sin(2π·t/T)` |
| Cosine | `mid + amp·cos(2π·t/T)` |
| Triangle | `mid + amp·(4φ−1)` if φ<0.5 else `mid + amp·(3−4φ)` where φ=mod(t,T)/T |
| Square | `hi` if φ<0.5 else `lo` |
| Trapezoid | 25% rise → 25% hold-high → 25% fall → 25% hold-low |

The time base uses `esp_timer_get_time()` (µs since boot), ensuring phase continuity and wakeup accuracy independent of FreeRTOS tick jitter. `vTaskDelayUntil` is used for the 1 Hz cadence.

### 5.4 CRC-16 Component — `components/crc16/`

The CRC-16 implementation is sourced directly from `src/lib/CRC16.h` and `src/lib/CRC16.cpp` of the parent project. The component's `CMakeLists.txt` registers it as an ESP-IDF component so it can be linked by `main`.

```c
/**
 * @brief Calculate CRC-16/CCITT-FALSE checksum.
 *
 * @param data   Pointer to data buffer.
 * @param length Number of bytes.
 * @return       16-bit CRC value.
 */
uint16_t calculateCRC16(const uint8_t* data, size_t length);
```

---

## 6. Logging Behaviour

All log output uses the standard `ESP_LOG` macros. The default log level is `INFO`.

### 6.1 `telemetryReceiverTask` (tag `TelemetryRx`)

| Event | Log level | Format |
|-------|-----------|--------|
| Task started | `INFO` | `"Telemetry Receiver Task started, listening on UART1 RX=GPIO5"` |
| Frame received, CRC OK | `INFO` | `"Frame OK: %s"` (payload string) |
| CRC mismatch | `ERROR` | `"CRC FAIL: received=0x%04X computed=0x%04X payload='%s'"` |
| Buffer overflow | `WARN` | `"Frame buffer overflow after %d bytes — discarding frame"` |
| Unexpected SOH mid-frame | `WARN` | `"Unexpected SOH mid-frame after %d bytes — restarting"` |
| Statistics | `INFO` | `"Stats: total=%u ok=%u crc_err=%u overflow=%u"` |

### 6.2 `sensorEmulatorTask` (tag `SensorEmul`)

| Event | Log level | Format |
|-------|-----------|--------|
| Task started | `INFO` | `"Sensor Emulator Task started — binary framed output on UART1 TX=GPIO4 @ 115200, sampled at %u ms"` |
| Packet transmitted | `INFO` | `"seq=%u payload=%d wire=%d crc=0x%04X"` |
| CRC inject active | `WARN` | `"CRC inject active — corrupting frame seq=%u"` |
| JSON buffer overflow | `ERROR` | `"JSON buffer overflow at seq=%u (len=%d)"` |

---

## 7. Build System

### 7.1 Toolchain

| Item | Requirement |
|------|-------------|
| SDK | ESP-IDF v5.x |
| Board | LOLIN S3 (ESP32-S3) |
| Build tool | `idf.py` or PlatformIO |
| Flash monitor | `idf.py monitor` or PlatformIO serial monitor |

### 7.2 ESP-IDF Project Structure

```
tests/TelemetryEmulator/
├── documentation/
│   ├── design.md               ← This document
│   ├── json.md                 ← JSON payload field reference and wire format
│   └── sensorData.md           ← MQTT sensor data specification
├── CMakeLists.txt              ← Top-level: cmake_minimum_required + project()
├── sdkconfig.defaults          ← Minimal config (log level, UART, no WiFi/BT)
├── main/
│   ├── CMakeLists.txt          ← idf_component_register(SRCS main.cpp ...)
│   ├── main.cpp
│   ├── frame_protocol.h        ← Shared framing constants (SOH, CAN, DLE)
│   ├── ledTask.h
│   ├── ledTask.cpp             ← WS2812B RGB LED indicator task
│   ├── telemetryReceiverTask.h
│   ├── telemetryReceiverTask.cpp
│   ├── sensorEmulatorTask.h
│   └── sensorEmulatorTask.cpp
├── tests/
│   ├── aggregatorClass/        ← Host-side Aggregator unit tests (MinGW / Code::Blocks)
│   │   ├── Aggregator.h            ← Header-only Aggregator class (authoritative copy)
│   │   ├── main.cpp                ← Catch2 test cases (11 cases, 63 assertions)
│   │   └── AggregatorTests.cbp     ← Code::Blocks project file
│   └── waveform/               ← Host-side waveform unit tests (MinGW / Code::Blocks)
│       ├── waveforms_standalone.h  ← Portable copy of wave_* functions (no ESP-IDF)
│       ├── main.cpp                ← Catch2 test cases (6 cases, 736 assertions)
│       └── WaveformTests.cbp       ← Code::Blocks project file
└── components/
    └── crc16/
        ├── CMakeLists.txt      ← idf_component_register(SRCS CRC16.cpp ...)
        ├── CRC16.h
        └── CRC16.cpp
```

### 7.3 Top-level `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(TelemetryEmulator)
```

### 7.4 `sdkconfig.defaults`

```
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=4096
# Disable unused subsystems to minimise flash size
CONFIG_BT_ENABLED=n
CONFIG_WIFI_ENABLED=n
```

### 7.5 Build & Flash Commands

```bash
cd tests/TelemetryEmulator
idf.py set-target esp32s3
idf.py build
idf.py -p COM<n> flash monitor
```

---

## 8. Error Handling

| Condition | Behaviour |
|-----------|----------|
| UART1 driver install fails | `ESP_LOGE` + `abort()` in `app_main()` |
| `xTaskCreate` fails (any task) | `ESP_LOGE`, init function returns `ESP_FAIL`; `app_main()` calls `abort()` |
| Unexpected `SOH` mid-frame | Reset state machine, restart from new `SOH`, `ESP_LOGW` |
| Frame buffer overflow | Discard frame, `ESP_LOGW`, increment `frames_overflow`, return to `WAIT_SOH` |
| CRC mismatch | Log full details with `ESP_LOGE`, increment `frames_crc_error`, continue |
| Payload not null-terminated | Emitter always produces ASCII payloads well within `EMUL_JSON_BUF_SIZE` (512 bytes); receiver null-terminates after `CAN` |
| JSON serialisation overflow | `ESP_LOGE`, packet skipped; sequence counter still incremented |

---

## 9. Design Decisions & Rationale

| Decision | Rationale |
|----------|-----------|
| LOLIN S3 (ESP32-S3) instead of Windows PC | Both boards are physically the same; using the same platform and SDK as the rest of the project eliminates a cross-platform layer. |
| FreeRTOS task + ESP-IDF UART driver | Consistent with the architecture of the main project; ring-buffered DMA-backed reads avoid byte drop at 115200 baud. |
| State machine with SOH/CAN boundaries | Directly mirrors the transmitter's framing; robust to partial reads from `uart_read_bytes`. |
| CRC validated on every frame | The primary purpose of this module is CRC verification; every frame must be checked. |
| `ESP_LOGE` for CRC failures | Errors appear in red in the IDF monitor and serial log, making failures immediately visible during testing. |
| Separate ESP-IDF component for CRC-16 | Keeps the CRC implementation in one place; the same source file can be shared with `src/lib/` in the parent project. |
| No WiFi / BT in `sdkconfig.defaults` | Minimises build time and flash footprint; this firmware does nothing over the network. |
| Deterministic waveforms for sensor emulation | Periodic signals with known analytical formulae make it trivial to verify receiver-side correctness: expected values at any timestamp can be computed offline. |
| Different periods per signal | Incommensurable periods (e.g. 5 s, 7 s, 3 s …) ensure all combinations of field values occur within a short observation window, maximising test coverage. |
| `esp_timer_get_time()` as time base | Provides µs resolution, monotonically increasing from boot, independent of FreeRTOS tick rate — ensures waveform phase is continuous and accurate. |
| `vTaskDelayUntil` for 1 Hz cadence | Compensates for execution time inside the task body so the inter-packet interval stays exactly 1 s regardless of JSON serialisation overhead. |
| `sensorEmulatorTask` uses same binary framing as `telemetryReceiverTask` | The TX output can be looped back to an RX input (or fed into any conformant NTRIP Client receiver) for end-to-end testing without a real GPS board. It also makes the two task halves symmetric and validates that the framing/CRC implementation is self-consistent. |
| JSON payload inside binary frame | The frame envelope is identical to the position-telemetry protocol; only the payload content differs (JSON sensor data rather than CSV position). Keeps the wire protocol uniform while allowing the payload to carry any ASCII data. |
| `uart_read_bytes` with `size=1` | Reading one byte per call with `portMAX_DELAY` blocks only until the next UART byte arrives, so the state machine processes each byte immediately and `Frame OK` is logged the instant `CAN` is received — no batching delay. |
| `uart_wait_tx_done()` after `uart_write_bytes()` | Ensures the `SensorEmul` log timestamp reflects the actual end-of-transmission rather than the moment bytes were queued. In loopback, this means `Frame OK` appears before the sender log, confirming the receiver processed the frame while the UART was still draining. |
| BOOT button (GPIO0) as CRC-inject trigger | Provides a hardware-controlled, hands-on way to exercise the CRC error path without modifying firmware. Holding the button corrupts every outgoing frame; releasing it restores clean frames immediately, exercising both the error and recovery paths at will. |

---

## 10. Loopback Self-Test

Because `sensorEmulatorTask` produces frames that are protocol-identical to those consumed by `telemetryReceiverTask`, the two tasks can be run in a **loopback configuration** on a single board with no external hardware other than a short wire.

### 10.1 Hardware Setup

Connect a single wire between:

| From | To |
|------|----|
| GPIO4 (UART1 TX — emulator output) | GPIO5 (UART1 RX — receiver input) |

No NTRIP Client board is required. Both tasks run on the same LOLIN S3 #2.

```
┌───────────────────────────────────────────────────────────┐
│  TelemetryEmulator — LOLIN S3 #2  (loopback mode)         │
│                                                           │
│   sensorEmulatorTask                                      │
│   UART1 TX → GPIO4 ──┐                                    │
│                      │  short wire on-board               │
│   UART1 RX ← GPIO5 ◄─┘                                    │
│   telemetryReceiverTask                                   │
│   → decode frame                                          │
│   → validate CRC-16                                       │
│   → ESP_LOGI / ESP_LOGE                                   │
│                                                           │
│   UART0 → USB debug monitor                               │
└───────────────────────────────────────────────────────────┘
```

### 10.2 Expected Behaviour

| Observation | Expected |
|-------------|----------|
| `TelemetryRx` log after each emitter packet | `Frame OK: {"seq":N,...}` |
| `TelemetryRx` CRC errors | **None** |
| `TelemetryRx` buffer overflows | None (JSON payload well within 512-byte `RECV_FRAME_BUF_SIZE`) |
| `SensorEmul` log per packet | `seq=N payload=P wire=W crc=0x????` (appears after `Frame OK`) |
| Packet rate | 1 frame/second from emulator; both tasks log 1 line/second |
| Log order per packet | `TelemetryRx: Frame OK` first, then `SensorEmul: seq=N` a few milliseconds later |

Any `CRC FAIL` line in the monitor output (without holding the BOOT button) indicates a bug in either the byte-stuffing encoder or the de-stuffing decoder and should be investigated immediately.

### 10.3 Procedure

1. Fit a short wire between GPIO4 and GPIO5 on the LOLIN S3.
2. Flash the TelemetryEmulator firmware normally (`idf.py -p COM<n> flash`).
3. Open the serial monitor (`idf.py -p COM<n> monitor`).
4. Verify the startup messages from both tasks appear.
5. Observe that `Frame OK` lines appear at ~1 Hz with no `CRC FAIL` messages.
6. After at least 100 frames, verify the statistics line shows `crc_err=0` and `overflow=0`.
7. Remove the wire; the receiver will go silent while the emulator continues transmitting (its packets are not echoed back).

### 10.4 CRC Injection Test

The BOOT button on the LOLIN S3 (GPIO0, active LOW) can be used at any time during the loopback test to verify that the receiver correctly detects CRC errors:

1. With the loopback wire fitted and the monitor open, confirm `Frame OK` lines are appearing at 1 Hz.
2. Hold the BOOT button down.
3. The monitor should immediately show alternating `SensorEmul: CRC inject active` and `TelemetryRx: CRC FAIL` lines instead of `Frame OK`.
4. Release the button — `Frame OK` lines resume on the next packet.
5. Verify the statistics line (every 100 frames) reflects the correct `crc_err` count.

### 10.5 Limitations

- The single-board loopback test (GPIO4 ↔ GPIO5) only validates the emulator's own encode/decode cycle and CRC handling. It does **not** test the NTRIP Client's JSON reception or MQTT forwarding — that requires the two-board integration setup described in §11.
- The loopback validates framing and CRC correctness but **not** payload content correctness — the receiver logs the raw JSON string but does not parse the field values.
- The emitter runs at 1 Hz by default; the NTRIP Client's `dataOutputTask` transmits at 10 Hz. A 10 Hz stress test requires either a second board or a software change to `CONFIG_EMUL_PERIOD_MS`.

---

## 11. Two-Board Integration Test

This section describes the full bilateral integration test: TelemetryEmulator ↔ NTRIP Client over two UART wires, with both directions exercised simultaneously and MQTT publication verified on the broker.

### 11.1 Purpose

| Direction | Source | Sink | What is tested |
|-----------|--------|------|----------------|
| Emulator → Client | `sensorEmulatorTask` (GPIO4 TX) | NTRIP Client sensor input function | JSON sensor data is correctly received, CRC-validated, and published to MQTT |
| Client → Emulator | NTRIP Client `dataOutputTask` (GPIO15 TX) | `telemetryReceiverTask` (GPIO5 RX) | NTRIP Client telemetry frames are correctly framed with a valid CRC-16 |

### 11.2 Hardware Setup

Connect two wires and a shared GND between the boards (**do not** fit the single-board loopback wire GPIO4 → GPIO5):

| Wire | From | To |
|------|------|----|
| Telemetry (Client → Emulator) | NTRIP Client GPIO15 (UART TX) | TelemetryEmulator GPIO5 (UART1 RX) |
| Sensor data (Emulator → Client) | TelemetryEmulator GPIO4 (UART1 TX) | NTRIP Client GPIO\<n\> (UART RX — see NTRIP Client project) |
| Ground | NTRIP Client GND | TelemetryEmulator GND |

Both links operate at 115200 baud, 8N1.

### 11.3 Expected Behaviour

| Observation | Expected |
|-------------|----------|
| `TelemetryRx` log (Emulator monitor) | `Frame OK: ...` at the NTRIP Client's `dataOutputTask` rate (typically 10 Hz) |
| `CRC FAIL` lines (Emulator monitor) | **None** during normal operation |
| MQTT broker — sensor topic | One message per `sensorEmulatorTask` cycle (1 Hz default) containing all JSON fields from `sensorData.md` |
| BOOT button held | `SensorEmul: CRC inject active` + NTRIP Client reports CRC error; MQTT publications pause for the duration |
| BOOT button released | `Frame OK` and MQTT publications resume on the next packet |

### 11.4 Procedure

1. Flash the TelemetryEmulator firmware to LOLIN S3 #2 and the NTRIP Client firmware (with `sensorInputTask` enabled) to LOLIN S3 #1.
2. Connect the two inter-board UART wires and shared GND as per §11.2.
3. Ensure the NTRIP Client is connected to WiFi and the MQTT broker is reachable.
4. Open the TelemetryEmulator USB serial monitor (`idf.py -p COM<n> monitor`).
5. Subscribe to the sensor-data MQTT topic on the broker (e.g. with `mosquitto_sub` or MQTT Explorer).
6. Power both boards.
7. Verify `Frame OK` lines appear on the TelemetryEmulator monitor at the expected rate with `crc_err=0`.
8. Verify sensor topic messages arrive on the broker at 1 Hz, each containing all expected JSON fields.
9. Hold the BOOT button: confirm `CRC inject active` on the emulator monitor, CRC-error logging on the NTRIP Client, and a pause in MQTT publications.
10. Release the button: confirm both `Frame OK` lines and MQTT publications resume on the next packet cycle.
11. After at least 100 frames, verify the TelemetryEmulator statistics line shows the expected `crc_err` count from step 9 and `overflow=0`.

---

## 12. Open Issues / Future Work

| # | Item | Priority |
|---|------|----------|
| 1 | **Set local time from received NTRIP Client frame.** `telemetryReceiverTask` already decodes the full CSV payload which contains a UTC timestamp (`YYYY-MM-DD HH:mm:ss.sss`, §3.3). Add a post-CRC-OK step that parses this timestamp and calls `settimeofday()` (POSIX) or `esp_sntp_set_sync_mode` / `adjtime()` to set the ESP32 system clock. This would give the TelemetryEmulator a GPS-disciplined time reference without requiring an SNTP server. | Medium |


