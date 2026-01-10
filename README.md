# Lolin S3 FreeRTOS RGB LED Project

A PlatformIO project for the Lolin S3 (ESP32-S3) board demonstrating FreeRTOS task creation with an RGB LED blinking pattern.

## Hardware

- **Board**: Lolin S3 (ESP32-S3)
- **RGB LED Pins**:
  - Red: GPIO 4
  - Green: GPIO 16
  - Blue: GPIO 17

## Features

- FreeRTOS task implementation for RGB LED control
- Cycles through 7 colors (Red, Green, Blue, Yellow, Cyan, Magenta, White)
- 500ms on/off blink pattern
- Serial debugging output at 115200 baud

## Setup Instructions

### 1. Install PlatformIO

If you haven't already, install PlatformIO:
- **VS Code**: Install the PlatformIO IDE extension
- **CLI**: `pip install platformio`

### 2. Build the Project

**Using VS Code (Recommended):**
- Click the checkmark (✓) icon in the bottom toolbar, or
- Open PlatformIO sidebar → Project Tasks → lolin_s3 → General → Build

**Using PowerShell:**
```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
```

### 3. Upload to Board

Connect your Lolin S3 board via USB.

**Using VS Code (Recommended):**
- Click the right arrow (→) icon in the bottom toolbar, or
- Open PlatformIO sidebar → Project Tasks → lolin_s3 → General → Upload

**Using PowerShell:**
```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run --target upload
```

### 4. Monitor Serial Output

**Using VS Code (Recommended):**
- Click the plug icon in the bottom toolbar, or
- Open PlatformIO sidebar → Project Tasks → lolin_s3 → Monitor

**Using PowerShell:**
```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" device monitor
```

## Project Structure

```
espressiveFreeRTOS/
├── platformio.ini          # PlatformIO configuration
├── src/
│   └── main.cpp           # Main application with FreeRTOS task
└── README.md              # This file
```

## Code Overview

The project creates a FreeRTOS task (`vBlinkRGBTask`) that:
1. Cycles through predefined RGB colors
2. Turns the LED on for 500ms
3. Turns the LED off for 500ms
4. Moves to the next color in the sequence

The main loop remains empty, as all work is handled by the FreeRTOS task.

## Customization

### Change Blink Speed

Modify the delay values in [main.cpp](src/main.cpp):
```cpp
vTaskDelay(pdMS_TO_TICKS(500)); // Change 500 to your preferred milliseconds
```

### Change Colors

Modify the `colors[]` array in [main.cpp](src/main.cpp) to use different colors or add your own.

### Adjust Task Priority

Change the priority parameter in `xTaskCreate()`:
```cpp
xTaskCreate(vBlinkRGBTask, "RGB_Blink", 2048, NULL, 1, NULL);
//                                                     ^ Priority (1 is low)
```

## Troubleshooting

### Missing intelhex Module Error
If you get `ModuleNotFoundError: No module named 'intelhex'`:
```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m pip install intelhex
```

### Board Not Detected
- Press and hold the BOOT button while plugging in the USB cable
- Try a different USB cable (must support data, not just power)

### Upload Errors
- Make sure no other program is using the serial port
- Try resetting the board before uploading

### LED Not Working
- Verify the GPIO pin numbers match your board's RGB LED
- Check that the LED is not damaged

## FreeRTOS Resources

- [FreeRTOS Documentation](https://www.freertos.org/Documentation/RTOS_book.html)
- [ESP-IDF FreeRTOS](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/freertos.html)
- [PlatformIO ESP32 Platform](https://docs.platformio.org/en/latest/platforms/espressif32.html)

## License

This is a template/example project - feel free to use and modify as needed.
