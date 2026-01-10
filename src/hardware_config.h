/**
 * @file hardware_config.h
 * @brief Centralized Hardware Configuration for ESP32-S3 Lolin S3 Board
 * 
 * This file contains all GPIO pin assignments and hardware-specific constants
 * for the ESP32-S3 based NTRIP/GPS/MQTT system. All pin assignments are fixed
 * in firmware and documented here for easy reference and potential board porting.
 * 
 * Board: Lolin S3 (ESP32-S3)
 * 
 * @author ESP32-S3 NTRIP/GPS/MQTT System
 * @date 2026
 */

#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <driver/gpio.h>
#include <driver/uart.h>

// =============================================================================
// UART Pin Assignments
// =============================================================================

/**
 * UART1 - Telemetry Data Output
 * Purpose: Transmit formatted telemetry data to external unit
 * Baud Rate: 115200
 * Direction: TX-only (RX pin defined but unused)
 */
#define TELEMETRY_UART_NUM      UART_NUM_1
#define TELEMETRY_TX_PIN        GPIO_NUM_15
#define TELEMETRY_RX_PIN        GPIO_NUM_16  // Unused, but reserved
#define TELEMETRY_BAUD_RATE     115200

/**
 * UART2 - GNSS Receiver Communication
 * Purpose: Bidirectional communication with GPS/GNSS module
 * Baud Rate: 460800
 * Direction: Bidirectional (TX for RTCM corrections, RX for NMEA data)
 */
#define GNSS_UART_NUM           UART_NUM_2
#define GNSS_TX_PIN             GPIO_NUM_17
#define GNSS_RX_PIN             GPIO_NUM_18
#define GNSS_BAUD_RATE          460800

// =============================================================================
// LED Pin Assignments
// =============================================================================

/**
 * Status LEDs for system indicators
 */
#define WIFI_LED                GPIO_NUM_46  ///< WiFi connection status
#define NTRIP_LED               GPIO_NUM_9   ///< NTRIP client connection status
#define MQTT_LED                GPIO_NUM_10  ///< MQTT client connection status
#define FIX_RTKFLOAT_LED        GPIO_NUM_11  ///< GNSS RTK Float fix status
#define FIX_RTK_LED             GPIO_NUM_12  ///< GNSS RTK Fixed status
#define STATUS_LED_PIN          GPIO_NUM_38  ///< Lolin S3 built-in RGB LED

// =============================================================================
// Button Pin Assignments
// =============================================================================

/**
 * User Button (if available)
 * Note: GPIO0 is typically BOOT button on ESP32-S3
 */
#define USER_BUTTON_PIN         GPIO_NUM_0   // BOOT button

// =============================================================================
// I2C Pin Assignments (Reserved for future use)
// =============================================================================

#define I2C_SDA_PIN             GPIO_NUM_21
#define I2C_SCL_PIN             GPIO_NUM_22

// =============================================================================
// SPI Pin Assignments (Not available - GPIOs used for LEDs)
// =============================================================================
// Note: GPIOs 10, 11, 12 are used for status LEDs, not available for SPI
// GPIO 13 available if needed for future expansion

#define SPI_MISO_PIN            GPIO_NUM_13  // Available for future use

// =============================================================================
// Pin Usage Summary
// =============================================================================
/*
 * GPIO Allocation Table:
 * ----------------------
 * GPIO 0  : BOOT Button (input with pullup)
 * GPIO 9  : NTRIP LED (status indicator)
 * GPIO 10 : MQTT LED (status indicator)
 * GPIO 11 : RTK Float LED (status indicator)
 * GPIO 12 : RTK Fixed LED (status indicator)
 * GPIO 13 : Available (reserved for future expansion)
 * GPIO 15 : UART1 TX (Telemetry output)
 * GPIO 16 : UART1 RX (Telemetry - unused but reserved)
 * GPIO 17 : UART2 TX (GNSS RTCM corrections)
 * GPIO 18 : UART2 RX (GNSS NMEA input)
 * GPIO 19 : USB D- (system reserved)
 * GPIO 20 : USB D+ (system reserved)
 * GPIO 21 : I2C SDA (reserved)
 * GPIO 22 : I2C SCL (reserved)
 * GPIO 38 : Status LED (Lolin S3 RGB LED)
 * GPIO 43 : UART0 TX (USB Serial/JTAG)
 * GPIO 44 : UART0 RX (USB Serial/JTAG)
 * GPIO 46 : WiFi LED (status indicator)
 * 
 * Note: GPIO 19, 20, 43, 44 are used by USB and should not be reassigned
 */

// =============================================================================
// Hardware Configuration Validation
// =============================================================================

// Pin conflict checks commented out due to preprocessor evaluation issues
// Pins are verified: UART1 (GPIO 15/16), UART2 (GPIO 17/18) - no conflicts
/*
#if GNSS_TX_PIN == TELEMETRY_TX_PIN || GNSS_TX_PIN == TELEMETRY_RX_PIN
    #error "GNSS UART pins conflict with Telemetry UART pins"
#endif

#if GNSS_RX_PIN == TELEMETRY_TX_PIN || GNSS_RX_PIN == TELEMETRY_RX_PIN
    #error "GNSS UART pins conflict with Telemetry UART pins"
#endif
*/

#endif // HARDWARE_CONFIG_H
