# Telemetry Data — Signals, Ranges & Accuracy

## 1. Accelerometer — ADXL345 (SPI)

**Collection interval: 100 Hz (every 10 ms), interrupt-driven**

| Signal | Unit | Range | Resolution / Accuracy |
|--------|------|-------|-----------------------|
| Acceleration X | raw counts | ±16 g | 13-bit, LSB = 3.9 mg |
| Acceleration Y | raw counts | ±16 g | 13-bit, LSB = 3.9 mg |
| Acceleration Z | raw counts | ±16 g | 13-bit, LSB = 3.9 mg |

Data is collected via an external interrupt (data-ready on INT0). No additional processing — raw 16-bit values reconstructed from two 8-bit register reads.

---

## 2. Motor Controller — GEMmotors G1.X (CAN bus)

**Collection interval: event-driven (CAN message arrival)**

Three CAN message IDs are received:

### CAN ID `0x064` — Motor Telemetry (MTL)

| Signal | Unit | Range | Resolution |
|--------|------|-------|------------|
| Motor Torque | Nm | -100 to +100 | `int16_t` |
| Motor RPM | RPM | -10000 to +10000 | 0.1 RPM per LSB (`int16_t`) |
| Motor Temperature | °C | `int8_t` range | 1°C |
| Control Value | - | `int16_t` | - |
| Control Mode | - | 1 bit | - |
| Motor Mode | - | 3 bits | - |
| SW Enable | - | 1 bit | - |
| Motor State | - | 2 bits | - |

### CAN ID `0x065` — Motor Power (MPW)

| Signal | Unit | Range | Resolution |
|--------|------|-------|------------|
| Motor Power | W | -1000 to +1000 | `int16_t` |
| Inverter Peak Current | A | `int16_t` | - |

### CAN ID `0x045` — Motor Control Command (THR)

| Signal | Unit | Notes |
|--------|------|-------|
| Throttle / Control Value | % | 0–100 (`uint8_t`) — driver throttle paddle |
| Control Mode | - | bits |
| Motor Mode | - | bits |
| SW Enable | - | bit |
| Debug Mode | - | bit |

---

## 3. Spectronik Protium Fuel Cell (UART — 57600 baud, 8N1)

**Collection interval: ~1 second (continuous streaming from fuel cell controller)**

| Signal | Key | Unit | Range | Notes |
|--------|-----|------|-------|-------|
| Fuel Cell Voltage | FC_V | V | e.g. 71.17 V | `float` |
| Fuel Cell Current | FC_A | A | e.g. 10.21 A | `float` |
| Fuel Cell Power | FC_W | W | e.g. 726.6 W | `float` |
| Energy Consumed | Energy | Wh | e.g. 298 Wh | `float` |
| Fuel Cell Temperature 1 | FCT1 | °C | e.g. 30.90°C | `float` |
| Fan Speed | FAN | % | 0–100 | `uint8_t` |
| H2 Pressure Sensor 1 | H2P1 | Bar | 0.00–1.00 (typical ~0.5) | `float`, 2 decimals |
| H2 Pressure Sensor 2 | H2P2 | Bar | 0.00–1.00 (typical ~0.5) | `float`, 2 decimals |
| H2 Tank Pressure | Tank-P | Bar | 0.00–500.00 | `float`, 2 decimals |
| H2 Tank Temperature | Tank-T | °C | e.g. 25.08°C | `float` |
| Supercapacitor Voltage | UCB_V | V | 0.0–100.0 | `float`, 1 decimal |
| Voltage Setpoint | V_Set | V | `float` | - |
| Current Setpoint | I_Set | A | `float` | - |
| Number of Cells | Number_of_cell | - | `uint16_t` | - |
| Stasis Selector | Stasis_selector | - | `uint8_t` | - |
| Stasis Valve 1 Pressure | STASIS_V1 | Bar | `float` | - |
| Stasis Valve 2 Pressure | STASIS_V2 | Bar | `float` | - |

---

## 4. GPS/GNSS (Timekeeping only)

**Interval: every 100 ms**

The GPS is **not used for position data** — only for time synchronization. It overrides the RTC clock to keep the STM32 timestamp accurate to millisecond level (32.768 kHz oscillator).

---

## MQTT Transmission

The ESP32-S3 (Data Transmission Unit) aggregates incoming data and publishes a JSON packet every **1 second** to the broker hosted on the HAN server.

| Property | Value |
|----------|-------|
| Transport | MQTT over WiFi/MiFi cellular |
| Topic | `hm25/telemetry` |
| Publish interval | 1 second |
| Format | Stringified JSON |

Only a **subset** of all collected signals is transmitted. The full Spectronik dataset is reduced to 5 fields; motor status/mode flags are dropped entirely.

### Envelope fields

| JSON key | Description | Type | Notes |
|----------|-------------|------|-------|
| `seq` | Message sequence number | integer | Increments per publish, used to detect missing packets |
| `tim` | Timestamp of latest data collection | string | Format: `HH:MM:SS.mmm` |

### Vehicle block (`vhl`)

| JSON key | Signal | Unit | Range | Resolution |
|----------|--------|------|-------|------------|
| `accX` | Acceleration X | raw counts | ±16 g (13-bit) | LSB = 3.9 mg |
| `accY` | Acceleration Y | raw counts | ±16 g (13-bit) | LSB = 3.9 mg |
| `accZ` | Acceleration Z | raw counts | ±16 g (13-bit) | LSB = 3.9 mg |
| `thr` | Throttle paddle | % | 0–100 | 1% (`uint8_t`) |

### Motor block (`mtr`)

| JSON key | Signal | Unit | Range | Resolution |
|----------|--------|------|-------|------------|
| `pwr` | Motor power | W | -1000 to +1000 | `int16_t` (1 W) |
| `rpm` | Motor speed | RPM | -10000 to +10000 | 0.1 RPM per LSB |
| `trq` | Motor torque | Nm | -100 to +100 | `int16_t` (1 Nm) |

### Spectronik block (`spc`)

| JSON key | Signal | Unit | Range | Resolution |
|----------|--------|------|-------|------------|
| `vsc` | Supercapacitor voltage | V | 0.0–100.0 | 0.1 V (1 decimal) |
| `tankP` | H2 tank pressure | Bar | 0.00–500.00 | 0.01 Bar (2 decimals) |
| `fan` | Fan speed | % | 0–100 | 1% (`uint8_t`) |
| `h2P1` | H2 pressure sensor 1 | Bar | 0.00–1.00 | 0.01 Bar (2 decimals) |
| `h2P2` | H2 pressure sensor 2 | Bar | 0.00–1.00 | 0.01 Bar (2 decimals) |

### Aggregation method

All signals published over MQTT are **averaged** across all samples received within the 1-second window using **Welford's online algorithm**:

```cpp
avg_ += (value - avg_) / (count_ + 1);
count_++;
```

This runs incrementally as each sample arrives, so no sample buffer is needed (O(1) memory per signal). On each timer tick the current average is read, the JSON is built, published, and the accumulators are reset.

No signal uses last-value, peak, or sum — every field in the JSON represents the **mean** of all samples in that window.

| Property | Detail |
|----------|--------|
| Method | Running average (Welford's online algorithm) |
| Scope | Every signal in `vhl`, `mtr`, and `spc` blocks |
| Reset | After every publish (every 1 second) |
| Behaviour when no data arrives | JSON is still published with whatever accumulator state exists |

### Example JSON payload

```json
{
  "seq": 4,
  "tim": "17:30:20.000",
  "vhl": { "accX": 90, "accY": 26, "accZ": -197, "thr": 45 },
  "mtr": { "pwr": 250, "rpm": 200, "trq": 55 },
  "spc": { "vsc": 55.32, "tankP": 250.3, "fan": 30, "h2P1": 0.52, "h2P2": 0.63 }
}
```

### What is NOT transmitted

The following signals are collected and stored on the SD card but are **not included** in the MQTT payload:

- Fuel cell voltage, current, power, energy consumed
- Fuel cell temperature
- H2 tank temperature
- Voltage setpoint, current setpoint
- Number of cells, stasis selector
- Stasis valve 1 & 2 pressures
- Motor temperature, control mode, motor mode, SW enable, motor state, debug mode
- Inverter peak current

---

## Persistent Storage — SD Card (OpenLog)

All sensor data is written to a microSD card via the OpenLog module as it arrives. There is no aggregation — every sample is stored immediately at the native collection rate of each sensor.

**Format per line:** `[Timestamp HH:MM:SS,mmm][Data CSV]\n`

Each line begins with the millisecond-accurate timestamp, followed by the identifier prefix and comma-separated values.

### Accelerometer (prefix: `ACC`) — 100 Hz

| Signal | Unit | Range | Resolution | Log interval |
|--------|------|-------|------------|-------------|
| Acceleration X | raw counts | ±16 g | 13-bit, LSB = 3.9 mg | 10 ms |
| Acceleration Y | raw counts | ±16 g | 13-bit, LSB = 3.9 mg | 10 ms |
| Acceleration Z | raw counts | ±16 g | 13-bit, LSB = 3.9 mg | 10 ms |

Example log line:
```
23:17:42,099ACC,-109,32,-229
```

### Motor Telemetry (prefix: `MTL`) — event-driven

| Signal | Unit | Range | Resolution |
|--------|------|-------|------------|
| Control Value | - | `int16_t` | 1 |
| Control Mode | - | 1 bit | - |
| Motor Mode | - | 3 bits | - |
| SW Enable | - | 1 bit | - |
| Motor State | - | 2 bits | - |
| Motor Torque | Nm | -100 to +100 | `int16_t` |
| Motor RPM | RPM | -10000 to +10000 | 0.1 RPM per LSB |
| Motor Temperature | °C | -128 to +127 | 1°C (`int8_t`) |

### Motor Power (prefix: `MPW`) — event-driven

| Signal | Unit | Range | Resolution |
|--------|------|-------|------------|
| Motor Power | W | -1000 to +1000 | `int16_t` |
| Inverter Peak Current | A | `int16_t` | - |

### Motor Control Command / Throttle (prefix: `THR`) — event-driven

| Signal | Unit | Range | Resolution |
|--------|------|-------|------------|
| Throttle / Control Value | % | 0–100 | `uint8_t` |
| Control Mode | - | bits | - |
| Motor Mode | - | bits | - |
| SW Enable | - | bit | - |
| Debug Mode | - | bit | - |

### Spectronik Fuel Cell (prefix: `SPC`) — ~1 s

All 17 fields are logged. This is the **full dataset** — more than what is sent over MQTT.

| Signal | Key | Unit | Range | Resolution |
|--------|-----|------|-------|------------|
| Fuel Cell Voltage | FC_V | V | e.g. 71.17 V | `float` |
| Fuel Cell Current | FC_A | A | e.g. 10.21 A | `float` |
| Fuel Cell Power | FC_W | W | e.g. 726.6 W | `float` |
| Energy Consumed | Energy | Wh | e.g. 298 Wh | `float` |
| Fuel Cell Temperature 1 | FCT1 | °C | e.g. 30.90°C | `float` |
| Fan Speed | FAN | % | 0–100 | `uint8_t` |
| H2 Pressure Sensor 1 | H2P1 | Bar | 0.00–1.00 (typical ~0.5) | `float`, 2 decimals |
| H2 Pressure Sensor 2 | H2P2 | Bar | 0.00–1.00 (typical ~0.5) | `float`, 2 decimals |
| H2 Tank Pressure | Tank-P | Bar | 0.00–500.00 | `float`, 2 decimals |
| H2 Tank Temperature | Tank-T | °C | e.g. 25.08°C | `float` |
| Supercapacitor Voltage | UCB_V | V | 0.0–100.0 | `float`, 1 decimal |
| Voltage Setpoint | V_Set | V | `float` | - |
| Current Setpoint | I_Set | A | `float` | - |
| Number of Cells | Number_of_cell | - | `uint16_t` | - |
| Stasis Selector | Stasis_selector | - | `uint8_t` | - |
| Stasis Valve 1 Pressure | STASIS_V1 | Bar | `float` | - |
| Stasis Valve 2 Pressure | STASIS_V2 | Bar | `float` | - |

### Storage medium

| Property | Value |
|----------|-------|
| Device | SparkFun OpenLog (ATmega328-based) |
| Interface | UART (serial), fully blocking writes |
| Storage | microSD, 64 MB – 64 GB, FAT16/FAT32 |
| Write trigger | Immediate on data arrival (no buffering delay beyond 512-byte SD block) |
| Max log files | 65,534 per root directory |

> **Note:** Individual fields within each sensor can be selectively disabled via compile-time `PARSE_...` flags in `Config.hpp`. Disabled fields are neither parsed nor written to the log.

---

## Summary of Collection Intervals

| Source | Interval | Trigger |
|--------|----------|---------|
| ADXL345 accelerometer | 10 ms (100 Hz) | Hardware interrupt |
| Motor CAN messages | Event-driven | CAN bus message arrival |
| Spectronik fuel cell | ~1 s | UART streaming |
| GPS time sync | 100 ms | Periodic |
| MQTT publish | 1 s | Timer-based aggregation |
