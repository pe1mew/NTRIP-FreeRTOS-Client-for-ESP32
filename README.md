# NTRIP FreeRTOS Client for ESP32
This project implements an NTRIP (Networked Transport of RTCM via Internet Protocol) client for the ESP32 platform using FreeRTOS. It is designed to receive GNSS correction data from an NTRIP caster and forward it to a GNSS receiver. The project is modular and supports configuration via a web interface.

## Features
- NTRIP client for RTCM correction data
- WiFi management and configuration
- HTTP server for configuration
- MQTT client for telemetry
- GNSS receiver integration
- Data output task for GNSS/RTCM data
- Statistics task for monitoring

## Directory Structure

```
src/
  configurationManagerTask.*   # Handles configuration logic
  dataOutputTask.*             # Handles output of GNSS/RTCM data
  gnssReceiverTask.*           # GNSS receiver integration
  httpServer.*                 # Web server for configuration
  main.cpp                     # Main application entry point
  mqttClientTask.*             # MQTT client for telemetry
  ntripClientTask.*            # NTRIP client logic
  statisticsTask.*             # System statistics
  wifiManager.*                # WiFi management
  lib/                         # Utility libraries (CRC16, etc.)
  NMEAparser/                  # NMEA sentence parsing
  NTRIPclient/                 # NTRIP protocol implementation
```

## Getting Started

1. Clone the repository.
2. Configure your build environment for ESP32 (ESP-IDF or PlatformIO).
3. Adjust configuration files as needed (`sdkconfig.defaults`, `hardware_config.h`).
4. Build and flash the firmware to your ESP32 device.

## Configuration

Configuration is managed via a web interface hosted on the ESP32. Connect to the device's WiFi AP or local network, then access the configuration page in your browser.

## FreeRTOS Resources

- [FreeRTOS Documentation](https://www.freertos.org/Documentation/RTOS_book.html)
- [ESP-IDF FreeRTOS](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/freertos.html)
- [PlatformIO ESP32 Platform](https://docs.platformio.org/en/latest/platforms/espressif32.html)

# License
See the [LICENSE](license.md) file for details.

This project is free: You can redistribute it and/or modify it under the terms of a Creative Commons Attribution-NonCommercial 4.0 International License (http://creativecommons.org/licenses/by-nc/4.0/) by Remko Welling (https://ese.han.nl/~rwelling) E-mail: remko.welling@han.nl

<a rel="license" href="http://creativecommons.org/licenses/by-nc/4.0/"><img alt="Creative Commons License" style="border-width:0" src="https://i.creativecommons.org/l/by-nc/4.0/88x31.png" /></a><br />This work is licensed under a <a rel="license" href="http://creativecommons.org/licenses/by-nc/4.0/">Creative Commons Attribution-NonCommercial 4.0 International License</a>.

# Disclaimer
This project is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.