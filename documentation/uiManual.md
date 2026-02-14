# ESP32 NTRIP Client - Web UI User Manual

## Table of Contents

1. [Getting Started](#getting-started)
2. [Accessing the Web Interface](#accessing-the-web-interface)
3. [Security and Authentication](#security-and-authentication)
4. [Configuration Sections](#configuration-sections)
   - [UI Password Configuration](#ui-password-configuration)
   - [WiFi Configuration](#wifi-configuration)
   - [NTRIP Client Configuration](#ntrip-client-configuration)
   - [MQTT Client Configuration](#mqtt-client-configuration)
5. [System Status Monitoring](#system-status-monitoring)
6. [Service Control](#service-control)
7. [System Management](#system-management)
8. [Password Recovery](#password-recovery)
9. [LED Status Indicators](#led-status-indicators)
10. [Troubleshooting](#troubleshooting)
11. [Default Configuration Values](#default-configuration-values)

---

## Getting Started

The ESP32 NTRIP Client is a professional-grade RTK correction streaming device with built-in web-based configuration. This system combines:
- **NTRIP Client**: Receives RTK correction data from an NTRIP caster
- **GNSS Receiver Interface**: Communicates with your GPS/GNSS receiver via UART
- **MQTT Publisher**: Publishes position and telemetry data to an MQTT broker
- **Data Output**: Sends formatted telemetry to external systems via serial port
- **Web Configuration Interface**: Easy setup and monitoring via web browser

---

## Accessing the Web Interface

### Initial Setup

1. **Power on the device**
   - The ESP32 will automatically create a WiFi Access Point (AP)

2. **Connect to the device's WiFi network**
   - **Network Name (SSID)**: `NTRIPClient-XXXX` (where XXXX are the last 4 hex digits of the device's MAC address)
   - **Example**: `NTRIPClient-A1B2`, `NTRIPClient-FF3C`
   - **Password**: `config123` (default, can be changed)
   - Look for this network in your device's WiFi settings
   - **Note**: The MAC address suffix allows you to identify multiple devices on the same network

3. **Open the web interface**
   - Open a web browser (Chrome, Firefox, Safari, Edge)
   - Navigate to: **http://192.168.4.1**
   - The configuration page will load automatically

4. **Login to the interface**
   - **Default Password**: `admin`
   - Enter the password and click "Login"
   - ⚠️ **Security Warning**: Change the default password immediately after first login!

### Network Architecture

The device operates in **dual WiFi mode**:

- **Access Point (AP) Mode**: Always active
  - SSID: `NTRIPClient-XXXX` (XXXX = last 4 hex digits of MAC address)
  - Example: `NTRIPClient-A1B2`
  - IP Address: `192.168.4.1`
  - Purpose: Configuration interface (always accessible)
  - **Unique Identifier**: Each device has a unique MAC-based suffix for identification

- **Station (STA) Mode**: Connects to your WiFi router
  - IP Address: Assigned by your router via DHCP
  - Purpose: Internet connectivity for NTRIP and MQTT services

**Important**: The configuration interface remains accessible via `192.168.4.1` even when the device is connected to your router.

---

## Security and Authentication

### UI Password Protection

The web interface is protected by password authentication to prevent unauthorized access to device configuration.

**Security Features**:
- Session-based authentication with timeout
- Password stored securely in non-volatile storage
- Factory default password warning displayed until changed
- Password recovery via hardware button (see [Password Recovery](#password-recovery))

**Best Practices**:
1. Change the default password immediately after first use
2. Use a strong password (8+ characters, mix of letters/numbers/symbols)
3. Do not share the Access Point password with untrusted users
4. Regularly review and update passwords

---

## Configuration Sections

The web interface is organized into logical sections. Each section can be configured independently and saved.

### UI Password Configuration

**Purpose**: Secure access to the web configuration interface

**Location**: Top section of the configuration page

#### Parameters

| Parameter | Description | Default | Format |
|-----------|-------------|---------|--------|
| **UI Password** | Password for web interface access | `admin` | 1-63 characters |

#### Configuration Steps

1. Enter your new password in the "UI Password" field
2. Click "Save UI Password" button
3. The system will save immediately (no restart required)
4. Use the new password for future logins

#### Important Notes

- ⚠️ A warning banner will display if you're still using the default password `admin`
- Changing the UI password does NOT affect WiFi passwords
- If you forget your password, use the [Password Recovery](#password-recovery) procedure
- The password is case-sensitive

---

### WiFi Configuration

**Purpose**: Connect the device to your home or office WiFi network for internet access

**Location**: Second section of the configuration page

#### Parameters

| Parameter | Description | Default | Format | Required |
|-----------|-------------|---------|--------|----------|
| **WiFi SSID** | Name of your WiFi network (STA mode) | `YourWiFiSSID` | 1-31 characters | Yes |
| **WiFi Password** | Password for your WiFi network (STA mode) | `YourWiFiPassword` | 8-63 characters | Yes |
| **AP SSID** | Device's Access Point name (read-only) | `NTRIPClient-XXXX`* | Display only | No |
| **AP Password** | Password for device's Access Point | `config123` | 8-63 characters | Yes |

\* *The AP SSID is automatically generated as `NTRIPClient-` followed by the last 2 bytes (4 hex digits) of the device's MAC address. This creates a unique identifier for each device. Example: `NTRIPClient-A1B2`*

#### Configuration Steps

1. **Find your WiFi network details**
   - SSID (network name) - exactly as it appears
   - Password - WiFi is case-sensitive

2. **Enter WiFi credentials**
   - **WiFi SSID**: Enter your router's network name (for STA mode - internet access)
   - **WiFi Password**: Enter your WiFi password (for STA mode)
   - **AP SSID**: Display only - shows your device's unique Access Point name (e.g., `NTRIPClient-A1B2`)
     - This is automatically generated from the device's MAC address
     - Cannot be changed (ensures unique identification)
   - **AP Password**: Change from default `config123` for security

3. **Save configuration**
   - Click "Save WiFi Config" button
   - Device will automatically attempt to connect to your WiFi
   - Connection status will update in the System Status section

#### Configuration Tips

- **SSID is case-sensitive**: `MyNetwork` is different from `mynetwork`
- **Hidden networks**: You can connect to hidden networks by typing the SSID exactly
- **5GHz vs 2.4GHz**: The ESP32-S3 supports both bands
- **Signal strength**: Check the System Status section for RSSI (signal strength)
  - RSSI > -50 dBm: Excellent
  - RSSI -50 to -70 dBm: Good
  - RSSI -70 to -80 dBm: Fair
  - RSSI < -80 dBm: Poor (consider moving device closer to router)

#### Understanding AP SSID Format

The Access Point SSID follows this format: **`NTRIPClient-AABB`**

- **Base name**: `NTRIPClient` (fixed)
- **MAC suffix**: `-AABB` (unique per device)
- **Source**: Last 2 bytes of the device's WiFi MAC address
- **Example**: If MAC address ends in `12:34:A1:B2`, the SSID will be `NTRIPClient-A1B2`

**Why MAC-based SSID?**
- **Unique identification**: Each device has a different SSID
- **Multi-device environments**: Easily distinguish between multiple devices
- **Network management**: Identify specific devices in your WiFi list
- **No conflicts**: Prevents SSID collisions on the same network

#### Fallback Behavior

If the device cannot connect to your WiFi:
- The Access Point remains active with its unique SSID
- You can still access the configuration page at `192.168.4.1`
- Check the System Status to see connection errors
- Verify SSID and password are correct

---

### NTRIP Client Configuration

**Purpose**: Connect to an NTRIP caster to receive RTK correction data for high-precision GPS positioning

**Location**: Third section of the configuration page

#### What is NTRIP?

NTRIP (Networked Transport of RTCM via Internet Protocol) is a protocol for streaming GPS correction data from base stations. This correction data enables your GPS receiver to achieve centimeter-level accuracy (RTK - Real-Time Kinematic positioning).

#### Parameters

| Parameter | Description | Default | Format | Range | Required |
|-----------|-------------|---------|--------|-------|----------|
| **NTRIP Host** | NTRIP caster server address | `rtk2go.com` | Hostname or IP | - | Yes |
| **NTRIP Port** | NTRIP caster server port | `2101` | Number | 1-65535 | Yes |
| **NTRIP Mountpoint** | Specific base station on caster | `YourMountpoint` | String | 1-63 chars | Yes |
| **NTRIP User** | Username for authentication | `user` | String | 1-31 chars | Yes |
| **NTRIP Password** | Password for authentication | `password` | String | 1-63 chars | Yes |
| **GGA Interval (sec)** | How often to send position to caster | `120` | Number | 10-600 | No |
| **Reconnect Delay (sec)** | Wait time before reconnecting | `5` | Number | 1-60 | No |
| **Enabled** | Enable/disable NTRIP client | `false` | Checkbox | - | - |

#### Configuration Steps

1. **Obtain NTRIP caster credentials**
   - Register with an NTRIP service provider (e.g., RTK2GO, commercial services)
   - Note your credentials: host, port, mountpoint, username, password

2. **Enter NTRIP configuration**
   - **NTRIP Host**: Enter the caster server address
     - Example: `rtk2go.com`, `ntrip.geodetics.com`, or an IP address
   - **NTRIP Port**: Usually `2101` (standard NTRIP port)
   - **NTRIP Mountpoint**: Name of the base station closest to you
   - **NTRIP User**: Your username (may be "user" or your email for some services)
   - **NTRIP Password**: Your password (may be "password" for free services)

3. **Configure timing parameters**
   - **GGA Interval**: How often to send your position to the caster
     - Recommended: `120` seconds (2 minutes)
     - Some casters require position updates to provide corrections
     - Lower values increase bandwidth usage slightly
   - **Reconnect Delay**: Wait time after connection loss before retry
     - Recommended: `5` seconds
     - Prevents rapid reconnection attempts

4. **Enable the service**
   - Check the "Enabled" checkbox
   - Click "Save NTRIP Config" button
   - The device will automatically connect to the NTRIP caster

5. **Verify connection**
   - Check System Status section for "NTRIP Connected: Yes"
   - Check "RTCM Bytes Received" counter (should be increasing)
   - Watch the NTRIP LED indicator (should be blinking when receiving data)

#### Finding the Right Mountpoint

**For RTK2GO (free service)**:
1. Visit: http://rtk2go.com:2101 (in a web browser)
2. Find a base station near your location (within 50km for best results)
3. Note the mountpoint name from the list
4. Use credentials: User: `user`, Password: `password` (for most RTK2GO stations)

**For Commercial Services**:
- Contact your service provider for credentials
- They will provide: host, port, mountpoint, username, and password

#### Configuration Tips

- **Choose nearby base station**: Closer stations provide better corrections
  - Optimal: < 10 km
  - Good: 10-30 km
  - Acceptable: 30-50 km
  - Poor: > 50 km
- **GGA interval timing**:
  - Most casters accept 60-300 second intervals
  - `120` seconds is a good balance
- **Authentication**: Some free services use generic credentials (`user`/`password`)
- **Multiple mountpoints**: You can only connect to one mountpoint at a time
- **Network required**: NTRIP requires an active internet connection via WiFi STA mode

#### Troubleshooting NTRIP Connection

**Connection fails immediately**:
- Check WiFi connection status (must be connected to internet)
- Verify host and port are correct
- Try pinging the host from your computer

**Authentication errors (HTTP 401/403)**:
- Verify username and password
- Some services use email address as username
- Check if your account is active/paid (for commercial services)

**Connection succeeds but no data (RTCM Bytes = 0)**:
- Verify mountpoint name is exactly correct (case-sensitive)
- Check if the base station is active (check caster's website)
- Some mountpoints require GGA position updates to start streaming

**Frequent disconnections**:
- Check WiFi signal strength (RSSI)
- Increase Reconnect Delay if caster is rate-limiting
- Verify internet connection is stable

---

### MQTT Client Configuration

**Purpose**: Publish GPS position, system status, and statistics to an MQTT broker for remote monitoring and data logging

**Location**: Fourth section of the configuration page

#### What is MQTT?

MQTT (Message Queuing Telemetry Transport) is a lightweight messaging protocol ideal for IoT devices. The ESP32 publishes real-time position data and system health information to an MQTT broker, which can be accessed by:
- Dashboard applications
- Data logging systems
- Fleet management software
- Custom monitoring solutions

#### Published Topics

The device publishes three types of messages to MQTT:

| Topic | Content | Default Interval | Purpose |
|-------|---------|------------------|---------|
| `<base>/GNSS` | GPS position data | 10 seconds | Real-time location tracking |
| `<base>/status` | System health status | 120 seconds | Overall system health |
| `<base>/stats` | Performance statistics | 60 seconds | Detailed metrics |

Where `<base>` is your configured base topic (e.g., `ntripclient`).

#### Parameters

| Parameter | Description | Default | Format | Range | Required |
|-----------|-------------|---------|--------|-------|----------|
| **MQTT Broker** | MQTT broker server address | `mqtt.example.com` | Hostname or IP | - | Yes |
| **MQTT Port** | MQTT broker port | `1883` | Number | 1-65535 | Yes |
| **MQTT Topic** | Base topic for publishing | `ntripclient` | String | 1-63 chars | Yes |
| **MQTT User** | Username for broker authentication | `mqttuser` | String | 0-31 chars | No* |
| **MQTT Password** | Password for broker authentication | `mqttpassword` | String | 0-63 chars | No* |
| **GNSS Interval (sec)** | Position publish interval | `10` | Number | 0-300 | No |
| **Status Interval (sec)** | Status publish interval | `120` | Number | 0-600 | No |
| **Stats Interval (sec)** | Statistics publish interval | `60` | Number | 0-600 | No |
| **Enabled** | Enable/disable MQTT client | `false` | Checkbox | - | - |

\* Required if your broker requires authentication

#### Configuration Steps

1. **Set up an MQTT broker**

   You can use:
   - **Cloud MQTT services**: HiveMQ Cloud, CloudMQTT, AWS IoT Core
   - **Self-hosted**: Mosquitto, EMQX (on Raspberry Pi, server, or NAS)
   - **Public test brokers**: test.mosquitto.org (unencrypted, for testing only)

2. **Enter MQTT broker details**
   - **MQTT Broker**: Enter broker hostname or IP address
     - Example: `mqtt.example.com`, `broker.hivemq.com`, `192.168.1.100`
   - **MQTT Port**:
     - `1883` for unencrypted MQTT
     - `8883` for MQTT over TLS/SSL (if supported)
   - **MQTT Topic**: Choose a base topic name
     - Example: `ntripclient`, `rtk/device01`, `fleet/vehicle123`
     - Must be unique if multiple devices share the same broker
   - **MQTT User**: Username if broker requires authentication
     - Leave empty for public/anonymous brokers
   - **MQTT Password**: Password if broker requires authentication
     - Leave empty for public/anonymous brokers

3. **Configure publish intervals**

   Set how often each type of message is published:

   - **GNSS Interval**: Real-time position updates
     - `10` seconds = frequent updates, higher bandwidth
     - `30` seconds = moderate updates
     - `60` seconds = infrequent updates, lower bandwidth
     - `0` = disable position publishing

   - **Status Interval**: System health snapshots
     - `120` seconds (2 minutes) = default
     - Includes WiFi, NTRIP, MQTT connection status
     - `0` = disable status publishing

   - **Stats Interval**: Detailed performance metrics
     - `60` seconds (1 minute) = default
     - Includes RTK fix quality, HDOP, satellite counts, error rates
     - `0` = disable statistics publishing

4. **Enable the service**
   - Check the "Enabled" checkbox
   - Click "Save MQTT Config" button
   - Device will automatically connect to the MQTT broker

5. **Verify connection**
   - Check System Status section for "MQTT Connected: Yes"
   - Use an MQTT client to subscribe to topics:
     - `<your_topic>/GNSS`
     - `<your_topic>/status`
     - `<your_topic>/stats`

#### Published Message Formats

All messages are published in **JSON format** for easy parsing.

**Example GNSS Position Message** (`ntripclient/GNSS`):
```json
{
   "num": 1357720,
   "daytime": "2025-03-28 10:27:06.200",
   "lat": 52.211483,
   "lon": 5.983663,
   "alt": 21.394,
   "fix_type": 5,
   "speed": 0.0,
   "dir": 334.2,
   "sats": 31,
   "hdop": 0.48,
   "age": 2.2
}
```

**Field Descriptions**:
- `num`: Message sequence number (increments from boot)
- `daytime`: Timestamp in ISO 8601 format
- `lat`: Latitude in decimal degrees
- `lon`: Longitude in decimal degrees
- `alt`: Altitude above sea level in meters
- `fix_type`: GPS fix quality (0=No fix, 1=GPS, 2=DGPS, 4=RTK Fixed, 5=RTK Float)
- `speed`: Ground speed in meters per second
- `dir`: True heading in degrees (0-360)
- `sats`: Number of satellites in use
- `hdop`: Horizontal Dilution of Precision (lower is better)
- `age`: Age of differential correction in seconds

**Example System Status Message** (`ntripclient/status`):
```json
{
   "timestamp": "2025-03-28 10:27:06.200",
   "uptime_sec": 7325,
   "heap_free": 245678,
   "wifi": {
      "connected": true,
      "rssi_dbm": -65
   },
   "ntrip": {
      "connected": true,
      "uptime_sec": 7120,
      "rtcm_packets_total": 8234
   },
   "mqtt": {
      "connected": true,
      "uptime_sec": 7200,
      "messages_published": 732
   },
   "gnss": {
      "current_fix": 4
   }
}
```

**Example Statistics Message** (`ntripclient/stats`):
```json
{
   "timestamp": "2025-03-28 10:27:06.200",
   "period_sec": 60,
   "rtcm": {
      "bytes_received": 9856,
      "message_rate": 3,
      "data_gaps": 0
   },
   "gnss": {
      "fix_duration": {
         "no_fix": 0,
         "gps": 0,
         "dgps": 0,
         "rtk_float": 8,
         "rtk_fixed": 52
      },
      "rtk_fixed_percent": 86.7,
      "hdop_avg": 0.52,
      "sats_avg": 29
   },
   "wifi": {
      "rssi_avg": -67,
      "uptime_percent": 100.0
   },
   "errors": {
      "nmea_checksum": 0,
      "uart": 0,
      "rtcm_queue_overflow": 0
   }
}
```

#### Configuration Tips

- **Broker selection**:
  - For testing: Use `test.mosquitto.org` (port 1883, no auth)
  - For production: Use a private broker with authentication
  - For cloud: Consider HiveMQ Cloud (free tier available)

- **Topic naming convention**:
  - Use hierarchical topics: `company/site/device/data`
  - Keep topic names short but descriptive
  - Avoid spaces and special characters

- **Interval tuning**:
  - Lower intervals = more data, higher bandwidth, higher broker load
  - Higher intervals = less frequent updates, lower bandwidth
  - Set to `0` to disable specific message types you don't need

- **Bandwidth estimation**:
  - GNSS message: ~200 bytes
  - Status message: ~250 bytes
  - Stats message: ~300 bytes
  - Example (default intervals): ~3 KB/minute

- **Security considerations**:
  - Always use authentication on production brokers
  - Consider using TLS/SSL (port 8883) for encrypted communication
  - Change default topic name for security through obscurity

#### MQTT Client Applications

Subscribe to your device's data using:

**Command-line tools**:
```bash
# Mosquitto client (Linux/Mac/Windows)
mosquitto_sub -h mqtt.example.com -p 1883 -u mqttuser -P mqttpassword -t "ntripclient/#" -v

# Subscribe to specific topic
mosquitto_sub -h mqtt.example.com -p 1883 -u mqttuser -P mqttpassword -t "ntripclient/GNSS"
```

**GUI tools**:
- MQTT Explorer (Windows/Mac/Linux) - highly recommended
- MQTT.fx (Java-based)
- MyMQTT (Android)
- MQTTool (iOS)

**Programming**:
- Python: `paho-mqtt` library
- Node.js: `mqtt` package
- Node-RED: Built-in MQTT nodes

#### Troubleshooting MQTT Connection

**Connection fails**:
- Verify WiFi is connected to internet
- Check broker hostname/IP is correct
- Verify port number (usually 1883 or 8883)
- Test broker with MQTT client from your computer

**Authentication errors**:
- Verify username and password
- Check if broker requires authentication
- Some brokers are case-sensitive

**Connected but no data**:
- Check if intervals are set to 0 (disabled)
- Verify topics are correct (use wildcard `#` to see all)
- Check System Status for "Messages Published" counter

**Intermittent disconnections**:
- Check WiFi signal strength
- Verify broker is stable (check broker logs)
- Consider increasing keep-alive time (firmware default: 60s)

---

## System Status Monitoring

The web interface displays real-time system status at the top of the page. This section updates automatically without refreshing the page.

### Status Indicators

#### WiFi Status
- **Connected**: Device is connected to your WiFi router
- **Disconnected**: Not connected (check WiFi configuration)
- **IP Address**: IP address assigned by your router (displayed when connected)
- **RSSI**: Signal strength in dBm (displayed when connected)
  - -30 to -50 dBm: Excellent
  - -50 to -70 dBm: Good
  - -70 to -80 dBm: Fair
  - -80 to -90 dBm: Poor
  - Below -90 dBm: Very poor (may have connectivity issues)

#### NTRIP Status
- **Connected**: Successfully connected to NTRIP caster
- **Disconnected**: Not connected (check NTRIP configuration or internet)
- **RTCM Bytes Received**: Total bytes of correction data received since boot
  - Should be continuously increasing when connected
  - Typical rate: 50-200 bytes/second

#### MQTT Status
- **Connected**: Successfully connected to MQTT broker
- **Disconnected**: Not connected (check MQTT configuration or internet)
- **Messages Published**: Total number of MQTT messages sent since boot

#### System Information
- **Uptime**: Time since last boot in human-readable format (e.g., "2h 34m 15s")
- **Free Heap**: Available RAM in bytes
  - Normal range: 100,000 - 250,000 bytes
  - Warning if < 50,000 bytes (possible memory leak)

### Status Update Frequency

The status section automatically refreshes every **5 seconds** while you have the configuration page open. You do not need to manually refresh the browser.

---

## Service Control

### Runtime Enable/Disable Toggle

You can enable or disable NTRIP and MQTT services without changing their configuration or restarting the device.

**How to use**:
1. Locate the "Enabled" checkbox in the NTRIP or MQTT configuration section
2. Check the box to enable, uncheck to disable
3. Click the corresponding "Save" button
4. The service will start or stop immediately

**Use cases**:
- Temporarily disable NTRIP to save bandwidth
- Disable MQTT when not actively monitoring
- Test configuration changes without full restart
- Quickly turn services on/off for troubleshooting

**What happens when disabled**:
- Service stops cleanly (closes connections)
- No data is transmitted or received
- Configuration is preserved
- Re-enabling reconnects with saved settings

---

## System Management

### Restart Device

Performs a clean restart of the ESP32 device.

**How to use**:
1. Scroll to the bottom of the configuration page
2. Click the "Restart Device" button
3. Confirm the action
4. Device will restart in approximately 3 seconds
5. Wait 30-60 seconds for full boot
6. Refresh browser to reconnect

**When to restart**:
- After changing critical configuration
- To apply firmware updates
- To clear persistent errors
- As a troubleshooting step

**What happens during restart**:
- All network connections are closed
- Configuration is saved to non-volatile storage
- Device performs a clean reboot
- WiFi AP will briefly disappear then reappear
- All services restart automatically

### Factory Reset

Erases **all configuration** and restores device to factory default settings.

**⚠️ WARNING**: This operation is **irreversible**. All configuration will be lost.

**What is reset**:
- UI password → `admin`
- WiFi configuration → Default placeholders
- NTRIP configuration → Default placeholders
- MQTT configuration → Default placeholders
- All services → Disabled by default

**What is preserved**:
- Firmware version (not erased)
- Hardware settings (pins, baud rates)

**How to use**:
1. Scroll to the bottom of the configuration page
2. Click the "Factory Reset" button
3. **Read the warning carefully**
4. Confirm the action
5. Device will erase configuration and restart
6. Wait for restart to complete
7. Reconnect to WiFi AP with default password: `config123`
8. Login with default UI password: `admin`
9. Reconfigure all settings

**When to use factory reset**:
- Selling or transferring device to another user
- Completely starting over with configuration
- Device is in an unknown or corrupted state
- As a last resort troubleshooting step

**Alternative**: If you only need to reset the UI password, use the [Password Recovery](#password-recovery) procedure instead (does not erase other settings).

---

## Password Recovery

If you forget the UI password, you can reset it without losing other configuration.

### Hardware Button Reset Method

**Requirements**:
- Physical access to the device
- Access to the BOOT button (GPIO 0)

**Procedure**:
1. **Locate the BOOT button** on the ESP32 board
   - Usually labeled "BOOT" or "IO0"
   - Often located near the USB connector

2. **Press and hold the BOOT button**
   - Hold for **5 seconds continuously**
   - Do not release early

3. **Observe the RGB LED**
   - The LED may change color or pattern to indicate reset

4. **Release the button**
   - The UI password is now reset to `admin`

5. **Reconnect to web interface**
   - Navigate to http://192.168.4.1
   - Login with password: `admin`
   - Immediately change the password to a new value

**Important notes**:
- Only the UI password is reset (other settings are preserved)
- WiFi, NTRIP, and MQTT configurations are **not affected**
- This does NOT perform a full factory reset
- The device does not need to restart

### What if button reset doesn't work?

If the hardware button method fails:
1. Perform a full Factory Reset via web interface (if accessible)
2. Re-flash the firmware using USB connection
3. Contact support for assistance

---

## LED Status Indicators

The device has **six LED indicators** providing visual feedback on system status.

### Discrete LEDs

| LED | GPIO | Status Indication | LED States |
|-----|------|-------------------|------------|
| **WiFi LED** | 46 | WiFi STA connection status | ON = Connected to WiFi router<br>OFF = Disconnected |
| **NTRIP LED** | 9 | NTRIP client status | OFF = Disabled or disconnected<br>ON (solid) = Connected, no recent data<br>BLINK = Connected and receiving RTCM data |
| **MQTT LED** | 10 | MQTT client status | OFF = Disabled or disconnected<br>ON (solid) = Connected, idle<br>BLINK = Publishing or receiving messages |
| **FIX_RTK_LED** | 12 | GPS fix status | OFF = No GPS fix<br>ON (solid) = Valid GPS fix (any quality) |
| **FIX_RTKFLOAT_LED** | 11 | RTK solution status | OFF = No RTK solution<br>BLINK = RTK Float fix (partial convergence)<br>ON (solid) = RTK Fixed (full accuracy) |

### RGB LED (Neopixel)

**Location**: Built-in on Lolin S3 board (GPIO 38)

**Status Colors**:

| Color | Meaning | Description |
|-------|---------|-------------|
| **OFF** | Initializing or error | System starting up or critical error |
| **GREEN** | Normal operation | All configured services running correctly |
| **YELLOW** | Partial operation | Some services disabled or WiFi disconnected |
| **RED** | Critical error | Configuration error or system failure |
| **BLUE (pulsing)** | Firmware update | Firmware update in progress (future feature) |

### Understanding Blink Patterns

**Activity Blink** (NTRIP LED, MQTT LED):
- **Blink Trigger**: Data received/sent within last 2 seconds
- **Blink Rate**: 1 Hz (500ms on, 500ms off)
- Indicates active data flow

**RTK Float Blink** (FIX_RTKFLOAT_LED):
- **Blink Rate**: 1 Hz (500ms on, 500ms off)
- Indicates RTK solution converging (not yet fixed)

### LED Troubleshooting

**WiFi LED not turning on**:
- Check WiFi configuration (SSID and password)
- Verify WiFi router is powered on and in range
- Check RSSI value in System Status (signal strength)

**NTRIP LED solid (not blinking)**:
- Connected but not receiving data
- Check mountpoint name is correct
- Verify base station is active
- Check if caster requires GGA updates

**MQTT LED never blinks**:
- Check if publish intervals are too long
- Verify MQTT broker is reachable
- Check if messages are being published (System Status counter)

**FIX_RTK_LED stays off**:
- GPS receiver not connected or powered
- Check UART cable connections
- Verify GPS receiver baud rate (460800)
- GPS may not have satellite lock yet (can take 30-300 seconds outdoors)

**FIX_RTKFLOAT_LED stays blinking**:
- Normal behavior during RTK convergence (can take 1-10 minutes)
- Check if NTRIP is connected and receiving data
- Verify base station distance (should be < 50 km)
- Check number of satellites (need 8+ for RTK)

**RGB LED shows RED**:
- Check System Status for specific error
- Review configuration for errors
- Check free heap memory (low memory can cause issues)
- Try restarting the device

---

## Troubleshooting

### Common Issues and Solutions

#### Cannot Access Web Interface

**Problem**: Cannot open http://192.168.4.1

**Solutions**:
1. Verify you're connected to the device's WiFi network
   - Look for network named `NTRIPClient-XXXX` (where XXXX are 4 hex digits)
   - Example: `NTRIPClient-A1B2`, `NTRIPClient-FF3C`
   - Each device has a unique MAC-based suffix
2. Check device is powered on (look for LEDs)
3. Try a different web browser
4. Disable VPN or proxy on your device
5. Try accessing from a different device (phone, tablet, computer)
6. Restart the ESP32 device
7. Perform factory reset via hardware button

#### WiFi Won't Connect

**Problem**: Device won't connect to home WiFi (WiFi LED stays off)

**Solutions**:
1. Verify SSID and password are exactly correct (case-sensitive)
2. Check WiFi router is powered on and broadcasting
3. Verify WiFi is 2.4GHz or 5GHz (both supported by ESP32-S3)
4. Move device closer to router (check RSSI in System Status)
5. Check router's MAC address filtering (whitelist the ESP32)
6. Try a different WiFi network (phone hotspot) to isolate issue
7. Verify router DHCP is enabled
8. Check router firewall settings

#### NTRIP Won't Connect

**Problem**: NTRIP LED stays off or solid (not blinking)

**Solutions**:
1. Verify WiFi is connected first (NTRIP requires internet)
2. Check NTRIP configuration is correct:
   - Host, port, mountpoint, username, password
3. Verify credentials with NTRIP provider
4. Check if base station is active (visit caster's website)
5. Try a different mountpoint
6. Check if your account has expired (for paid services)
7. Verify System Status shows "NTRIP Connected"
8. Check RTCM bytes counter is increasing

#### MQTT Won't Connect

**Problem**: MQTT LED stays off

**Solutions**:
1. Verify WiFi is connected first (MQTT requires internet)
2. Check MQTT configuration:
   - Broker address, port, username, password
3. Test broker from your computer using MQTT client
4. Verify broker is online and accessible
5. Check authentication credentials
6. Try port 1883 (unencrypted) before trying 8883 (TLS)
7. Disable authentication temporarily to test connection
8. Check broker logs for connection attempts

#### No GPS Fix

**Problem**: FIX_RTK_LED stays off, no position data

**Solutions**:
1. **GPS antenna outdoors**: GPS requires clear sky view
2. Wait 30-300 seconds for cold start
3. Check GPS receiver is powered on
4. Verify UART connections (TX/RX not swapped)
5. Check GPS baud rate is 460800
6. Use different GPS receiver to isolate hardware issue
7. Check GNSS data in System Status or MQTT messages

#### RTK Won't Converge

**Problem**: FIX_RTKFLOAT_LED blinks but never goes solid

**Solutions**:
1. Verify NTRIP is connected and receiving data (LED blinking)
2. Check RTCM bytes are increasing continuously
3. Verify base station distance (< 50 km ideal)
4. Wait longer (RTK can take 1-10 minutes to converge)
5. Check satellite count (need 8+ satellites)
6. Verify GPS receiver supports RTK (not all receivers do)
7. Check HDOP value (should be < 2.0 for RTK)
8. Try different mountpoint (closer base station)

#### High Memory Usage

**Problem**: Free Heap is very low (< 50,000 bytes)

**Solutions**:
1. Restart device to clear memory
2. Disable unused services (MQTT or NTRIP if not needed)
3. Increase publish intervals (reduce MQTT frequency)
4. Check for memory leaks (contact support if persistent)
5. Update to latest firmware

#### Device Keeps Restarting

**Problem**: Device reboots unexpectedly

**Solutions**:
1. Check power supply (must provide stable 5V, 500mA minimum)
2. Use quality USB cable (cheap cables can cause voltage drops)
3. Check for short circuits on connections
4. Review System Status for error messages before restart
5. Disable services one by one to isolate cause
6. Perform factory reset
7. Re-flash firmware

#### Configuration Changes Not Saving

**Problem**: Settings revert after restart

**Solutions**:
1. Click "Save" button after changing settings
2. Wait for confirmation message before closing browser
3. Check free heap memory (NVS writes require memory)
4. Perform factory reset to clear corrupted NVS
5. Re-flash firmware if NVS partition is corrupted

---

## Default Configuration Values

### Factory Default Settings

When the device is first powered on or after factory reset, the following default values are used:

#### UI Configuration
| Parameter | Default Value |
|-----------|---------------|
| UI Password | `admin` |

#### WiFi Configuration
| Parameter | Default Value | Notes |
|-----------|---------------|-------|
| WiFi SSID (STA) | `YourWiFiSSID` | Placeholder - must be changed |
| WiFi Password (STA) | `YourWiFiPassword` | Placeholder - must be changed |
| AP SSID | `NTRIPClient-XXXX` | Auto-generated from MAC address (unique per device) |
| AP Password | `config123` | Should be changed for security |

#### NTRIP Configuration
| Parameter | Default Value | Notes |
|-----------|---------------|-------|
| NTRIP Host | `rtk2go.com` | Popular free NTRIP caster |
| NTRIP Port | `2101` | Standard NTRIP port |
| NTRIP Mountpoint | `YourMountpoint` | Placeholder - must be changed |
| NTRIP User | `user` | Common for free services |
| NTRIP Password | `password` | Common for free services |
| GGA Interval | `120` seconds | Send position every 2 minutes |
| Reconnect Delay | `5` seconds | Wait 5 seconds before retry |
| Enabled | `false` | Disabled until configured |

#### MQTT Configuration
| Parameter | Default Value | Notes |
|-----------|---------------|-------|
| MQTT Broker | `mqtt.example.com` | Placeholder - must be changed |
| MQTT Port | `1883` | Standard MQTT port (unencrypted) |
| MQTT Topic | `ntripclient` | Base topic for all messages |
| MQTT User | `mqttuser` | Placeholder |
| MQTT Password | `mqttpassword` | Placeholder |
| GNSS Interval | `10` seconds | Position published every 10 seconds |
| Status Interval | `120` seconds | Status published every 2 minutes |
| Stats Interval | `60` seconds | Statistics published every minute |
| Enabled | `false` | Disabled until configured |

### Hardware Configuration (Fixed)

These parameters are fixed in firmware and cannot be changed via web interface:

#### UART Configuration
| Interface | Purpose | TX Pin | RX Pin | Baud Rate |
|-----------|---------|--------|--------|-----------|
| UART1 | Telemetry Output | GPIO 15 | GPIO 16 | 115200 |
| UART2 | GNSS Receiver | GPIO 17 | GPIO 18 | 460800 |

#### LED Pin Assignments
| LED | GPIO Pin | Type |
|-----|----------|------|
| WiFi LED | GPIO 46 | Discrete |
| NTRIP LED | GPIO 9 | Discrete |
| MQTT LED | GPIO 10 | Discrete |
| FIX_RTK_LED | GPIO 12 | Discrete |
| FIX_RTKFLOAT_LED | GPIO 11 | Discrete |
| RGB LED (Neopixel) | GPIO 38 | WS2812B |

#### Other Hardware
| Component | GPIO Pin | Notes |
|-----------|----------|-------|
| BOOT Button | GPIO 0 | Used for UI password reset |

---

## Best Practices

### Security Best Practices

1. **Change default passwords immediately**:
   - UI Password: Change from `admin`
   - AP Password: Change from `config123`

2. **Use strong passwords**:
   - Minimum 8 characters
   - Mix of uppercase, lowercase, numbers, symbols
   - Avoid common words or patterns

3. **Secure MQTT**:
   - Always use authentication on production brokers
   - Consider using TLS/SSL (port 8883)
   - Use unique topic names per device

4. **Physical security**:
   - Protect device from unauthorized physical access
   - BOOT button can reset UI password

5. **Network security**:
   - Use WPA2 or WPA3 encryption on your WiFi
   - Consider using a separate IoT network for ESP32
   - Regularly update router firmware

### Operational Best Practices

1. **Regular monitoring**:
   - Check System Status periodically
   - Monitor MQTT messages for anomalies
   - Review RSSI to ensure good WiFi signal

2. **Backup configuration**:
   - Document your configuration settings
   - Take screenshots of configuration page
   - Keep NTRIP and MQTT credentials in password manager

3. **Gradual configuration**:
   - Configure and test one service at a time
   - Start with WiFi → NTRIP → MQTT
   - Verify each service before enabling the next

4. **Power management**:
   - Use quality power supply (5V, 1A recommended)
   - Consider UPS for critical applications
   - Monitor free heap memory regularly

5. **Firmware updates**:
   - Check for firmware updates periodically
   - Read release notes before updating
   - Backup configuration before updating

### Performance Optimization

1. **NTRIP optimization**:
   - Choose closest base station (< 50 km)
   - Use GGA interval of 60-120 seconds
   - Monitor RTCM data rate

2. **MQTT optimization**:
   - Adjust publish intervals based on needs
   - Set intervals to 0 for unused message types
   - Use QoS 0 for position data (default)

3. **WiFi optimization**:
   - Position device for good signal (RSSI > -70 dBm)
   - Use 5GHz WiFi if available and router is close
   - Minimize WiFi interference sources

4. **GPS optimization**:
   - Place GPS antenna outdoors with clear sky view
   - Keep antenna away from metal obstructions
   - Use quality GPS antenna with ground plane

---

## Support and Resources

### Getting Help

If you encounter issues not covered in this manual:

1. **Check LED indicators** for visual status clues
2. **Review System Status** for connection states
3. **Follow troubleshooting steps** for your specific issue
4. **Check MQTT messages** for detailed error information
5. **Contact support** with specific error details

### Useful Information for Support

When contacting support, provide:
- Device model and firmware version
- **Device's unique AP SSID** (e.g., `NTRIPClient-A1B2`) - helps identify specific device
- Configuration settings (UI password excluded)
- System Status values
- LED indicator states
- Error messages or unexpected behavior
- Steps to reproduce the issue

### External Resources

**NTRIP Casters**:
- RTK2GO: http://rtk2go.com
- CORS Network: https://geodesy.noaa.gov/CORS/
- Commercial providers: Contact local surveying suppliers

**MQTT Brokers**:
- HiveMQ Cloud: https://www.hivemq.com/mqtt-cloud-broker/
- Mosquitto: https://mosquitto.org/
- EMQX: https://www.emqx.io/

**MQTT Clients**:
- MQTT Explorer: http://mqtt-explorer.com/
- Mosquitto command-line: https://mosquitto.org/download/

---

## Appendix: Quick Reference Card

### First-Time Setup Checklist

- [ ] Connect to WiFi AP: `NTRIPClient-XXXX` (XXXX = last 4 digits of MAC, password: `config123`)
- [ ] Open browser: http://192.168.4.1
- [ ] Login with default password: `admin`
- [ ] Change UI password
- [ ] Change AP password
- [ ] Note your device's unique AP SSID (displayed in WiFi config section)
- [ ] Configure WiFi STA (SSID and password for your router)
- [ ] Wait for WiFi STA connection
- [ ] Configure NTRIP (if using RTK corrections)
- [ ] Enable NTRIP service
- [ ] Configure MQTT (if using remote monitoring)
- [ ] Enable MQTT service
- [ ] Verify all status indicators show "Connected"
- [ ] Test GPS fix (FIX_RTK_LED should light)
- [ ] Monitor MQTT messages

### Configuration URLs

| Purpose | URL / Value |
|---------|-------------|
| Main configuration page | http://192.168.4.1 |
| Device Access Point SSID | `NTRIPClient-XXXX` (XXXX = last 4 hex digits of MAC) |
| Default AP Password | config123 |
| Default UI Password | admin |
| AP IP Address | 192.168.4.1 |

### Common Port Numbers

| Service | Port | Protocol |
|---------|------|----------|
| HTTP Server | 80 | HTTP |
| NTRIP Caster | 2101 | HTTP/TCP |
| MQTT (unencrypted) | 1883 | MQTT |
| MQTT (TLS/SSL) | 8883 | MQTTS |

### LED Quick Reference

| LED | ON Solid | Blinking | OFF |
|-----|----------|----------|-----|
| WiFi | Connected to router | - | Disconnected |
| NTRIP | Connected (no data) | Receiving RTCM | Disabled/Disconnected |
| MQTT | Connected (idle) | Publishing data | Disabled/Disconnected |
| FIX_RTK | Valid GPS fix | - | No fix |
| FIX_RTKFLOAT | RTK Fixed | RTK Float | No RTK |
| RGB | See color chart | - | Initializing/Error |

### GPS Fix Quality Values

| Value | Name | Meaning | Accuracy |
|-------|------|---------|----------|
| 0 | No Fix | No GPS signal | N/A |
| 1 | GPS/SPS | Standard GPS | 5-15 meters |
| 2 | DGPS | Differential GPS | 1-5 meters |
| 4 | RTK Fixed | RTK with integer ambiguity resolution | 1-3 cm |
| 5 | RTK Float | RTK without fixed ambiguities | 10-50 cm |

---

**Document Version**: 1.0
**Last Updated**: 2026-01-21
**Firmware Compatibility**: ESP32 NTRIP Client v1.0+
**Hardware**: Lolin S3 ESP32-S3 Board

---

*For technical support or firmware updates, please contact your device supplier or visit the project repository.*
