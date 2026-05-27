### Example JSON payload

Compact form (whitespace-free, as emitted on the wire — exactly one frame,
~770–780 bytes typical):

```json
{"seq":4,"tim":"17:30:20.000","vhl":{"acc":{"x":{"min":-120.50,"max":135.25,"avg":2.10},"y":{"min":-95.00,"max":110.75,"avg":-1.40},"z":{"min":920.00,"max":1080.50,"avg":1000.20}},"thr":{"val":{"min":0.0,"max":100.0,"avg":45.3},"ctrlMode":1,"mtrMode":3,"swEn":1,"dbgMode":0},"spd":{"min":0.00,"max":8.50,"avg":4.25},"lat":52.3676000,"lon":4.9041000},"mtr":{"mtl":{"ctrl":{"min":-12.30,"max":85.40,"avg":50.10},"ctrlMode":1,"mtrMode":3,"swEn":1,"state":2,"trq":{"min":-100.0,"max":100.0,"avg":15.5},"rpm":{"min":-2500.0,"max":8200.0,"avg":4500.0},"tmp":{"min":42.5,"max":47.3,"avg":45.1}},"mpw":{"pwr":{"min":-250.0,"max":750.5,"avg":400.2},"cur":{"min":0.00,"max":180.50,"avg":95.30}}},"spc":{"fan":35,"h2P1":0.52,"h2P2":0.48,"tankP":250.30,"vsc":78.5,"fsa":25.40}}
```

Pretty-printed for readability (whitespace **not** transmitted):

```json
{
  "seq": 4,
  "tim": "17:30:20.000",
  "vhl": {
    "acc": {
      "x": {"min":-120.50,"max":135.25,"avg":2.10},
      "y": {"min":-95.00, "max":110.75,"avg":-1.40},
      "z": {"min":920.00, "max":1080.50,"avg":1000.20}
    },
    "thr": {
      "val": {"min":0.0,"max":100.0,"avg":45.3},
      "ctrlMode": 1, "mtrMode": 3, "swEn": 1, "dbgMode": 0
    },
    "spd": {"min":0.00,"max":8.50,"avg":4.25},
    "lat": 52.3676000,
    "lon": 4.9041000
  },
  "mtr": {
    "mtl": {
      "ctrl": {"min":-12.30,"max":85.40,"avg":50.10},
      "ctrlMode": 1, "mtrMode": 3, "swEn": 1, "state": 2,
      "trq": {"min":-100.0,"max":100.0,"avg":15.5},
      "rpm": {"min":-2500.0,"max":8200.0,"avg":4500.0},
      "tmp": {"min":42.5,"max":47.3,"avg":45.1}
    },
    "mpw": {
      "pwr": {"min":-250.0,"max":750.5,"avg":400.2},
      "cur": {"min":0.00,"max":180.50,"avg":95.30}
    }
  },
  "spc": {
    "fan": 35,
    "h2P1": 0.52,
    "h2P2": 0.48,
    "tankP": 250.30,
    "vsc": 78.5,
    "fsa": 25.40
  }
}
```

The JSON layout mirrors the CSV payload tags defined in
[framedCRCString.md](framedCRCString.md): `acc` ↔ `ACC`, `mtl` ↔ `MTL`,
`mpw` ↔ `MPW`, `thr` ↔ `THR`, `spc` ↔ `SPC`.

## Field Reference

### Aggregation structure

Fields that carry `{min, max, avg}` sub-objects are aggregated across all
samples received within the publish window (default 1 s, set via
`CONFIG_EMUL_PERIOD_MS`).

| Sub-key | Description |
|---------|-------------|
| `min` | Minimum sample value in the window |
| `max` | Maximum sample value in the window |
| `avg` | Running mean (Welford's online algorithm) |

Scalar fields carry the most recent sample directly (no aggregation), matching
the CSV-protocol convention for last-received values:

- `vhl.lat`, `vhl.lon` (sourced from NTRIP receiver)
- `vhl.thr.ctrlMode`, `vhl.thr.mtrMode`, `vhl.thr.swEn`, `vhl.thr.dbgMode`
- `mtr.mtl.ctrlMode`, `mtr.mtl.mtrMode`, `mtr.mtl.swEn`, `mtr.mtl.state`
- All `spc.*` fields

Discrete enum / state fields (the `*Mode`, `swEn`, `state` keys) are generated
by a slow cycler that steps through each enum's value range with periods
between 30 s and 90 s, so the receiver can verify decoding without rapid
churn.

---

### Numeric formatting (current implementation)

All floats are emitted by `snprintf` with fixed decimal precision. Receivers
parsing the JSON should accept the precision below — values are zero-padded
on the fractional side but **not** on the integer side.

| Field(s) | Format | Decimal places | Example |
|----------|--------|:--:|---------|
| `vhl.acc.{x,y,z}.{min,max,avg}` | `%.2f` | 2 | `1000.20` |
| `vhl.thr.val.{min,max,avg}` | `%.1f` | 1 | `45.3` |
| `vhl.thr.{ctrlMode,mtrMode,swEn,dbgMode}` | `%d` | (integer) | `1` |
| `vhl.spd.{min,max,avg}` | `%.2f` | 2 | `4.25` |
| `vhl.lat`, `vhl.lon` | `%.7f` | 7 | `52.3676000` |
| `mtr.mtl.ctrl.{min,max,avg}` | `%.2f` | 2 | `-12.30` |
| `mtr.mtl.{ctrlMode,mtrMode,swEn,state}` | `%d` | (integer) | `2` |
| `mtr.mtl.{trq,rpm,tmp}.{min,max,avg}` | `%.1f` | 1 | `4500.0` |
| `mtr.mpw.pwr.{min,max,avg}` | `%.1f` | 1 | `400.2` |
| `mtr.mpw.cur.{min,max,avg}` | `%.2f` | 2 | `95.30` |
| `spc.fan` | `%d` | (integer) | `35` |
| `spc.h2P1`, `spc.h2P2`, `spc.tankP`, `spc.fsa` | `%.2f` | 2 | `250.30` |
| `spc.vsc` | `%.1f` | 1 | `78.5` |
| `seq` | `%u` | (unsigned int) | `4` |
| `tim` | string `HH:MM:SS.mmm` | — | `17:30:20.000` |

---

### Envelope

| Key | Type | Description |
|-----|------|-------------|
| `seq` | integer | Sequence number, increments per publish; used to detect missing packets |
| `tim` | string | Timestamp of latest data collection, format `HH:MM:SS.mmm` |

---

### `vhl` block — Vehicle

> Columns: **Emulator range** is what the current implementation actually
> emits on the wire. **Sensor / spec range** is the underlying signal's
> design range (per `documentation/sensorData.md` and
> `documentation/framedCRCString.md`). Where the two differ, the emulator
> deliberately uses a narrower band to keep test waveforms representative.

#### `vhl.acc` — Accelerometer (CSV tag `ACC`)

| Key | Structure | Signal | Unit | Emulator range | Sensor / spec range | Source |
|-----|-----------|--------|------|---------------|--------------------|--------|
| `x` | `{min, max, avg}` | Acceleration X | raw counts | -4096 … +4096 | ±4096 (13-bit ADXL345) | ADXL345 SPI |
| `y` | `{min, max, avg}` | Acceleration Y | raw counts | -4096 … +4096 | ±4096 (13-bit ADXL345) | ADXL345 SPI |
| `z` | `{min, max, avg}` | Acceleration Z | raw counts | -512 … +4096 (gravity-biased) | ±4096 (13-bit ADXL345) | ADXL345 SPI |

#### `vhl.thr` — Motor Control Command / Throttle (CSV tag `THR`)

| Key | Structure | Signal | Unit | Emulator range | Sensor / spec range | Source |
|-----|-----------|--------|------|---------------|--------------------|--------|
| `val` | `{min, max, avg}` | Throttle paddle / control_value | % | 0.0 … 100.0 | 0–100 (`uint8_t`) | CAN ID `0x045` |
| `ctrlMode` | scalar `int` | Control mode | enum | cycle 0…1 | 1 bit | last received |
| `mtrMode` | scalar `int` | Motor mode | enum | cycle 0…7 | 3 bits | last received |
| `swEn` | scalar `int` | Software enable | flag | cycle 0…1 | 1 bit | last received |
| `dbgMode` | scalar `int` | Debug mode | flag | cycle 0…1 | 1 bit | last received |

#### `vhl.spd`, `vhl.lat`, `vhl.lon` — Vehicle dynamics

| Key | Structure | Signal | Unit | Emulator range | Notes | Source |
|-----|-----------|--------|------|---------------|-------|--------|
| `spd` | `{min, max, avg}` | Vehicle speed | m/s | 0.00 … 10.00 | non-spec extra (not in `framedCRCString.md`) | synthetic |
| `lat` | scalar `float` | Latitude | decimal degrees | signed, ≤ 1.1 cm precision at equator | non-spec extra | NTRIP receiver |
| `lon` | scalar `float` | Longitude | decimal degrees | signed, ≤ 1.1 cm precision at equator | non-spec extra | NTRIP receiver |

---

### `mtr` block — Motor Controller (GEMmotors G1.X, CAN bus)

#### `mtr.mtl` — Motor Telemetry (CSV tag `MTL`, CAN ID `0x064`)

| Key | Structure | Signal | Unit | Emulator range | Sensor / spec range |
|-----|-----------|--------|------|---------------|--------------------|
| `ctrl` | `{min, max, avg}` | Control value | — | -100.00 … +100.00 | `int16_t` |
| `ctrlMode` | scalar `int` | Control mode | enum | cycle 0…1 | 1 bit |
| `mtrMode` | scalar `int` | Motor mode | enum | cycle 0…7 | 3 bits |
| `swEn` | scalar `int` | Software enable | flag | cycle 0…1 | 1 bit |
| `state` | scalar `int` | Motor state | enum | cycle 0…3 | 2 bits |
| `trq` | `{min, max, avg}` | Motor torque | Nm | -100.0 … +100.0 | -100 … +100 (`int16_t`) |
| `rpm` | `{min, max, avg}` | Motor speed | RPM | -10000.0 … +10000.0 | -10000 … +10000 (0.1 RPM/LSB) |
| `tmp` | `{min, max, avg}` | Motor temperature | °C | 25.0 … 85.0 (warm-up profile) | -128 … +127 (`int8_t`) |

#### `mtr.mpw` — Motor Power (CSV tag `MPW`, CAN ID `0x065`)

| Key | Structure | Signal | Unit | Emulator range | Sensor / spec range |
|-----|-----------|--------|------|---------------|--------------------|
| `pwr` | `{min, max, avg}` | Motor power | W | -1000.0 … +1000.0 | -1000 … +1000 (`int16_t`, 1 W) |
| `cur` | `{min, max, avg}` | Inverter peak current | A | 0.00 … 200.00 (positive only) | `int16_t` |

---

### `spc` block — Spectronik Protium Fuel Cell (CSV tag `SPC`, UART, 57600 baud, 8N1, ~1 s interval)

All `spc` fields are last-received scalars per the CSV-protocol convention
(`framedCRCString.md` §8.6): the Spectronik module delivers data
asynchronously at its own rate, so no aggregation is applied — every value
is the most recent sample at the publish tick.

| Key | Type | Signal | Spectronik key | Unit | Emulator range | Sensor / spec range |
|-----|------|--------|----------------|------|---------------|--------------------|
| `fan` | scalar `int` | Fan speed | `FAN` | % | 0 … 100 | 0–100 (`uint8_t`) |
| `h2P1` | scalar `float` | H2 Pressure Sensor 1 | `H2PressureSensor1` | bar | 0.30 … 0.70 | 0.00–1.00 (typical ~0.5) |
| `h2P2` | scalar `float` | H2 Pressure Sensor 2 | `H2PressureSensor2` | bar | 0.30 … 0.70 | 0.00–1.00 (typical ~0.5) |
| `tankP` | scalar `float` | H2 Tank Pressure | `H2TankPressure` | bar | 0.00 … 500.00 | 0.00–500.00 |
| `vsc` | scalar `float` | Supercapacitor voltage | `UCB_V` | V | 0.0 … 100.0 | 0.0–100.0 |
| `fsa` | scalar `float` | Fuel Cell Current | `FuelCellCurrent` (`FC_A`) | A | 0.0 … 50.0 | unbounded `float` (non-spec extra; PARSE_FUELCELLCURRENT is disabled in active SPC config) |

---

## Wire Format Specification

The JSON payload is serialised to a compact (whitespace-free) UTF-8 string, then wrapped in the binary frame described below before transmission over UART1 (115200 baud, 8N1, TX on GPIO4). Publish cadence is set by `CONFIG_EMUL_PERIOD_MS` (default 1000 ms, range 100–60000 ms; configured via `idf.py menuconfig` → TelemetryEmulator).

### Sizing — current implementation

| Quantity | Value |
|----------|-------|
| Observed payload length (full hybrid layout) | ~770 – 780 bytes (worst case ~835) |
| Emulator JSON buffer (`EMUL_JSON_BUF_SIZE`) | 1024 bytes (static BSS) |
| Emulator wire buffer (`EMUL_WIRE_BUF_SIZE`) | 2 × `EMUL_JSON_BUF_SIZE` + 6 = 2054 bytes (static BSS) |
| Receiver frame buffer (`RECV_FRAME_BUF_SIZE`) | 1024 bytes (static BSS) |
| UART line utilisation @ 1 Hz | ~7% of 115200 baud |
| UART line utilisation @ 10 Hz | ~70% of 115200 baud |

The large buffers were moved out of task stacks into BSS so growth in payload
size does not risk a stack overflow — both tasks are singletons.

### Frame structure

```
┌───────┬─────────────────────────────────┬──────────────┬──────────────┬───────┐
│ SOH   │ Stuffed JSON payload            │ Stuffed      │ Stuffed      │ CAN   │
│ 0x01  │ (compact UTF-8, variable length)│ CRC-H        │ CRC-L        │ 0x18  │
└───────┴─────────────────────────────────┴──────────────┴──────────────┴───────┘
  1 byte          N bytes (variable)           1–2 bytes      1–2 bytes   1 byte
```

| Field | Value | Notes |
|-------|-------|-------|
| SOH | `0x01` | Start of frame — **never** byte-stuffed |
| Stuffed payload | variable | Compact JSON string after byte stuffing |
| Stuffed CRC-H | 1–2 bytes | High byte of CRC-16 after stuffing |
| Stuffed CRC-L | 1–2 bytes | Low byte of CRC-16 after stuffing |
| CAN | `0x18` | End of frame — **never** byte-stuffed |

### Control byte constants

| Constant | Value | Meaning |
|----------|-------|---------|
| `FRAME_SOH` | `0x01` | Start of Header (Control-A) |
| `FRAME_CAN` | `0x18` | Cancel / end of frame (Control-X) |
| `FRAME_DLE` | `0x10` | Data Link Escape |

### Byte stuffing

Any payload byte or CRC byte whose value equals `SOH`, `CAN`, or `DLE` must be escaped. The transmitter inserts a `DLE` byte immediately **before** the conflicting byte. `SOH` and `CAN` framing bytes themselves are **never** stuffed.

**Encoding (transmitter):**
```
for each byte b in (payload ++ [CRC_H, CRC_L]):
    if b == SOH or b == CAN or b == DLE:
        emit DLE
    emit b
```

**Decoding (receiver):**
```
b = next_byte()
if b == DLE:
    data_byte = next_byte()   // literal value, not a control byte
else:
    data_byte = b
```

### CRC-16 checksum

| Property | Value |
|----------|-------|
| Algorithm | CRC-16/CCITT-FALSE |
| Polynomial | `0x1021` |
| Initial value | `0xFFFF` |
| Input / output reflection | none |
| Byte order on wire | High byte first, then low byte |
| Coverage | De-stuffed payload bytes only — CRC bytes are **not** included in the CRC computation |

The CRC is computed over the raw (de-stuffed) JSON string. Both the high byte and the low byte are then individually byte-stuffed before being appended to the frame.

**Receiver validation:**
1. Collect de-stuffed payload bytes into a buffer.
2. The last two bytes in the buffer (before `CAN`) are `CRC_H` and `CRC_L` after de-stuffing.
3. Compute `calculated = calculateCRC16(payload, payload_len)`.
4. If `calculated != (CRC_H << 8 | CRC_L)` → discard frame and log a CRC error.
