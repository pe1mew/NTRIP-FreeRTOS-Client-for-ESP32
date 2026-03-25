### Example JSON payload

```json
{
  "seq": 4,
  "tim": "17:30:20.000",
  "vhl": { "thr": {"min":0,"max":0,"avg":0}, "spd": {"min":0,"max":0,"avg":0}, "lat": 0.00, "lon": 0.00 },
  "mtr": { "pwr": {"min":0,"max":0,"avg":0}, "rpm": {"min":0,"max":0,"avg":0}, "trq": {"min":0,"max":0,"avg":0} },
  "spc": { "vsc": {"min":0,"max":0,"avg":0}, "fsa": {"min":0,"max":0,"avg":0}, "tankP": 250.3}
}
```

## Field Reference

### Aggregation structure

Fields that carry `{min, max, avg}` sub-objects are aggregated across all samples received within the 1-second publish window.

| Sub-key | Description |
|---------|-------------|
| `min` | Minimum sample value in the window |
| `max` | Maximum sample value in the window |
| `avg` | Running mean (Welford's online algorithm) |

Scalar fields (`lat`, `lon`, `tankP`) carry the latest received value directly.

---

### Envelope

| Key | Type | Description |
|-----|------|-------------|
| `seq` | integer | Sequence number, increments per publish; used to detect missing packets |
| `tim` | string | Timestamp of latest data collection, format `HH:MM:SS.mmm` |

---

### `vhl` block — Vehicle

| Key | Structure | Signal | Unit | Range | Resolution | Source |
|-----|-----------|--------|------|-------|------------|--------|
| `thr` | `{min, max, avg}` | Throttle paddle position (with cruise control) | % | 0–100 (`uint8_t`) | 1% | CAN ID `0x045` |
| `spd` | `{min, max, avg}` | Vehicle speed | m/s | — | — | — |
| `lat` | scalar `float` | Latitude | decimal degrees | signed | 7 decimal places (≤ 1.1 cm at equator) | — |
| `lon` | scalar `float` | Longitude | decimal degrees | signed | 7 decimal places (≤ 1.1 cm at equator) | — |

---

### `mtr` block — Motor Controller (GEMmotors G1.X, CAN bus)

| Key | Structure | Signal | Unit | Range | Resolution | CAN ID |
|-----|-----------|--------|------|-------|------------|--------|
| `pwr` | `{min, max, avg}` | Motor power | W | -1000 to +1000 | `int16_t` (1 W) | `0x065` |
| `rpm` | `{min, max, avg}` | Motor speed | RPM | -10000 to +10000 | 0.1 RPM per LSB (`int16_t`) | `0x064` |
| `trq` | `{min, max, avg}` | Motor torque | Nm | -100 to +100 | `int16_t` (1 Nm) | `0x064` |

---

### `spc` block — Spectronik Protium Fuel Cell (UART, 57600 baud, 8N1, ~1 s interval)

| Key | Structure | Signal | Spectronik key | Unit | Range | Resolution |
|-----|-----------|--------|----------------|------|-------|------------|
| `vsc` | `{min, max, avg}` | Supercapacitor voltage | `UCB_V` | V | 0.0–100.0 | 0.1 V (1 decimal) |
| `fsa` | `{min, max, avg}` | Fuel Cell Current | `FC_A` | A | e.g. 10.21 A | `float` |
| `tankP` | scalar `float` | H2 tank pressure | `Tank-P` | Bar | 0.00–500.00 | 0.01 Bar (2 decimals) |

---

## Wire Format Specification

The JSON payload is serialised to a compact (whitespace-free) UTF-8 string, then wrapped in the binary frame described below before transmission over UART1 (115200 baud, 8N1, TX on GPIO4).

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
