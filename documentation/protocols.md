# Protocols

## MQTT message



The NTRIP Client sends three types of messages to the MQTT broker:

1. **GNSS Position Message**: Contains current position and GNSS metadata.
2. **System Status Message**: Reports device, connection, and runtime status.
3. **Statistics Message**: Periodic summary of GNSS, RTCM, and system health statistics.

Below are example structures and field specifications for each message type (see src/mqttClientTask.h):


### GNSS Position Message
```json
{
   "num": 1357720,
   "daytime": "2025-03-28 10:27:06.200",
   "lat": 37.774929,
   "lon": -122.419416,
   "alt": 21.394,
   "fix_type": 5,
   "speed": 0.0,
   "dir": 334.2,
   "sats": 31,
   "hdop": 0.48,
   "age": 2.2
}
```

**Field specification**

| Field      | Type      | Description |
|------------|-----------|-------------|
| **num**    | Integer   | Incrementing message sequence number since client start |
| **daytime**| String    | Timestamp in ISO 8601 format (YYYY-MM-DD HH:mm:ss.SSS) |
| **lat**    | Float     | Latitude in decimal degrees (±90.000000) |
| **lon**    | Float     | Longitude in decimal degrees (±180.000000) |
| **alt**    | Float     | Altitude in meters above sea level (ASL) |
| **fix_type**| uint8_t  | GNSS fix quality (see table below) |
| **speed**  | Float     | Speed in m/s |
| **dir**    | Float     | True heading in degrees (0-359.99) |
| **sats**   | Integer   | Number of satellites in fix |
| **hdop**   | Float     | Horizontal dilution of precision |
| **age**    | Float     | Age of differential correction in seconds |
 


### System Status Message
```json
{
   "timestamp": "2026-01-12 08:38:08.200",
   "uptime_sec": 770,
   "heap_free": 214320,
   "heap_min": 202700,
   "wifi": {
      "rssi_dbm": -28,
      "reconnects": 0
   },
   "ntrip": {
      "connected": true,
      "uptime_sec": 764,
      "reconnects": 0,
      "rtcm_packets_total": 2457
   },
   "mqtt": {
      "uptime_sec": 750,
      "messages_published": 36
   },
   "gnss": {
      "current_fix": 4
   }
}
```

**Field specification**

| Field                | Type      | Description |
|----------------------|-----------|-------------|
| **timestamp**        | String    | ISO 8601 timestamp |
| **uptime_sec**       | Integer   | System uptime in seconds |
| **heap_free**        | Integer   | Current free heap bytes |
| **heap_min**         | Integer   | Minimum free heap since boot |
| **wifi.rssi_dbm**    | Integer   | WiFi signal strength (dBm) |
| **wifi.reconnects**  | Integer   | WiFi reconnect count |
| **ntrip.connected**  | Boolean   | NTRIP connection status |
| **ntrip.uptime_sec** | Integer   | NTRIP cumulative connection time (seconds) |
| **ntrip.reconnects** | Integer   | NTRIP reconnect count |
| **ntrip.rtcm_packets_total** | Integer | Total RTCM packets received |
| **mqtt.uptime_sec**  | Integer   | MQTT cumulative connection time (seconds) |
| **mqtt.messages_published** | Integer | Total MQTT messages published |
| **gnss.current_fix** | Integer   | Current GNSS fix quality code |


### Statistics Message
```json
{
   "timestamp": "2026-01-12 08:38:38.200",
   "period_sec": 60,
   "rtcm": {
      "bytes_received": 34816,
      "message_rate": 3,
      "data_gaps": 0,
      "avg_latency_ms": 0,
      "corrupted": 0
   },
   "gnss": {
      "fix_duration": {
         "no_fix": 0,
         "gps": 0,
         "dgps": 0,
         "rtk_float": 0,
         "rtk_fixed": 20
      },
      "rtk_fixed_percent": 2.5,
      "time_to_rtk_fixed_sec": 0,
      "fix_downgrades": 0,
      "fix_upgrades": 0,
      "hdop_avg": 0.45,
      "hdop_min": 0.44,
      "hdop_max": 0.47,
      "sats_avg": 34,
      "baseline_distance_km": 0.00,
      "update_rate_hz": 0
   },
   "gga": {
      "sent_count": 0,
      "failures": 0,
      "queue_overflows": 0
   },
   "wifi": {
      "rssi_avg": -27,
      "rssi_min": -28,
      "rssi_max": 0,
      "uptime_percent": 2.5
   },
   "errors": {
      "nmea_checksum": 0,
      "uart": 0,
      "rtcm_queue_overflow": 0,
      "ntrip_timeouts": 0
   }
}
```

**Field specification**

| Field                        | Type      | Description |
|------------------------------|-----------|-------------|
| **timestamp**                | String    | ISO 8601 timestamp |
| **period_sec**               | Integer   | Statistics period duration (seconds) |
| **rtcm.bytes_received**      | Integer   | RTCM bytes received in period |
| **rtcm.message_rate**        | Integer   | RTCM message rate (messages/sec) |
| **rtcm.data_gaps**           | Integer   | RTCM data gaps detected |
| **rtcm.avg_latency_ms**      | Integer   | Average RTCM latency (ms) |
| **rtcm.corrupted**           | Integer   | Corrupted RTCM messages |
| **gnss.fix_duration.no_fix** | Integer   | Seconds in no fix state |
| **gnss.fix_duration.gps**    | Integer   | Seconds in GPS fix state |
| **gnss.fix_duration.dgps**   | Integer   | Seconds in DGPS fix state |
| **gnss.fix_duration.rtk_float** | Integer | Seconds in RTK float state |
| **gnss.fix_duration.rtk_fixed** | Integer | Seconds in RTK fixed state |
| **gnss.rtk_fixed_percent**   | Float     | Percentage in RTK fixed this period |
| **gnss.time_to_rtk_fixed_sec** | Integer | Time to achieve RTK fixed (seconds) |
| **gnss.fix_downgrades**      | Integer   | Number of fix quality downgrades |
| **gnss.fix_upgrades**        | Integer   | Number of fix quality upgrades |
| **gnss.hdop_avg**            | Float     | Average HDOP |
| **gnss.hdop_min**            | Float     | Minimum HDOP |
| **gnss.hdop_max**            | Float     | Maximum HDOP |
| **gnss.sats_avg**            | Integer   | Average satellites in fix |
| **gnss.baseline_distance_km**| Float     | Baseline distance to NTRIP caster (km) |
| **gnss.update_rate_hz**      | Integer   | GNSS update rate (Hz) |
| **gga.sent_count**           | Integer   | GGA messages sent |
| **gga.failures**             | Integer   | GGA send failures |
| **gga.queue_overflows**      | Integer   | GGA queue overflows |
| **wifi.rssi_avg**            | Integer   | Average WiFi RSSI (dBm) |
| **wifi.rssi_min**            | Integer   | Minimum WiFi RSSI (dBm) |
| **wifi.rssi_max**            | Integer   | Maximum WiFi RSSI (dBm) |
| **wifi.uptime_percent**      | Float     | WiFi uptime percentage |
| **errors.nmea_checksum**     | Integer   | NMEA checksum errors |
| **errors.uart**              | Integer   | UART errors |
| **errors.rtcm_queue_overflow** | Integer | RTCM queue overflows |
| **errors.ntrip_timeouts**    | Integer   | NTRIP timeouts |


#### Date-time format
Date time format is following the **ISO 8601** standard for databases, log files, and time-sensitive applications:
```
YYYY-MM-DD HH:mm:ss.sss
```
The format **YYYY-MM-DD HH:mm:ss.sss** represents a **timestamp** with the following components:  

- **YYYY** → 4-digit year (e.g., 2025)  
- **MM** → 2-digit month (01-12)  
- **DD** → 2-digit day of the month (01-31)  
- **HH** → 2-digit hour in 24-hour format (00-23)  
- **mm** → 2-digit minutes (00-59)  
- **ss** → 2-digit seconds (00-59)  
- **sss** → 3-digit milliseconds (000-999)  

#### Latitude longitude format

Latitude and longitude is written in **degrees decimal degrees (DDD)** who are geographic coordinates used to specify locations on Earth. They are expressed as:  

### **Format:**  
```
Latitude:   ±DD.DDDDDD
Longitude: ±DDD.DDDDDD
```

- **Latitude (Lat)**: Ranges from **-90.000000** to **+90.000000**  
  - **Positive (+)** → North of the Equator  
  - **Negative (-)** → South of the Equator  
- **Longitude (Lon)**: Ranges from **-180.000000** to **+180.000000**  
  - **Positive (+)** → East of the Prime Meridian  
  - **Negative (-)** → West of the Prime Meridian  

### **Example:**  
- **Latitude:** 37.774929 → **37.774929° N**  
- **Longitude:** -122.419416 → **122.419416° W**  


#### NMEA 0183 - GGA Sentence Fix Types
The `fix_type` field uses the NMEA GGA fix quality codes:

| Code | Description |
|------|-------------|
| 0    | No fix (invalid) |
| 1    | GPS fix (standard) |
| 2    | DGPS (Differential GPS) fix |
| 3    | PPS (Precise Positioning Service) fix |
| 4    | RTK (Real-Time Kinematic) fixed integer |
| 5    | RTK float |
| 6    | Estimated (dead reckoning) fix |
| 7    | Manual input mode |
| 8    | Simulation mode |


## Time message to telemetry unit
The message format is as follows:

 - **Start-byte**: Control-A (0x01)
 - **Timestamp**: (string) formatted as Date-time format (as decribed above)
 - **CRC-16**: (2 bytes, big-endian) over Timestamp (Start-byte excluded)
 - **Stop-byte**: Control-X (0x18)

```
   1                n               2      1    <- bytes in payload
[0x01][YYYY-MM-DD HH:MM:SS.SSSS][CRC-16][0x18]
```

For a **24-byte string**, **CRC-16** is often the better choice than **CRC-32**, unless you require extra-strong error detection.  


**Comparison of CRC-16 vs. CRC-32 for a 15-byte string**  

| Feature      | **CRC-16** | **CRC-32** |
|-------------|-----------|-----------|
| **Checksum Size** | 16 bits (2 bytes) | 32 bits (4 bytes) |
| **Error Detection** | Good for small data, detects single-bit and burst errors | Stronger, but overkill for small data |
| **Collision Probability** | Lower risk for short data | Even lower, but unnecessary for 15 bytes |
| **Processing Speed** | Faster (less computational overhead) | Slightly slower due to larger checksum |

**Why CRC-16 is Better in this application?**:
1. **Sufficient Error Detection**:  
   - For small payloads like 24 bytes, **CRC-16 is strong enough** to catch most transmission errors.  
   - CRC-32 provides better protection but is **not needed** unless extremely high reliability is required.  
2. **Lower Overhead**:  
   - CRC-16 adds only **2 bytes**, while CRC-32 adds **4 bytes** to the message.  
   - This makes CRC-16 more efficient in bandwidth-limited or low-memory environments.  
3. **Faster Computation**:  
   - CRC-16 is **lighter** in processing, making it ideal for embedded systems or low-power devices.  

**Conclusion**: For a **24-byte string**, **CRC-16 is the better choice** in most cases due to its efficiency and sufficient error detection.

### CRC-16 and Byte Stuffing Rationale

CRC-16 is used for telemetry messages because it provides strong error detection for small payloads, with minimal overhead and fast computation—ideal for embedded systems. The protocol applies byte stuffing to ensure framing bytes (SOH, CAN, DLE) do not appear in the message or CRC fields unescaped, maintaining reliable parsing and transmission integrity.

**Summary:**
- CRC-16 (2 bytes) is efficient and robust for short messages.
- Byte stuffing prevents accidental frame boundary collisions.
- The protocol is optimized for bandwidth-limited and low-power environments.
