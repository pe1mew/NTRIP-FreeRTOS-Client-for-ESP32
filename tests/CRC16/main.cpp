/*! 
 * @file main.cpp
 * @brief Main file for testing CRC-16 checksum calculation.
 * @details This file contains unit tests for the CRC-16 checksum calculation function.
 * @author Remko Welling (remko.welling@han.nl)
 * @date 30-3-2025
 * @note This implementation is based on the CRC-16-CCITT (XMODEM) algorithm.
 * 
 * ## Test Cases:
 * 
 * ### Assertions: 
 * 
 * 1. **CRC-16 for ASCII '12345'**: This test verifies the correctness of the CRC calculation for a common string.
 *    The data is represented in ASCII format, and the expected CRC is precomputed.
 * 
 * 2. **CRC-16 for empty data**: This test ensures that the function correctly handles an empty input.
 *    The expected CRC value for an empty dataset is the initial value of the CRC-16-CCITT algorithm (0xFFFF).
 * 
 * 3. **CRC-16 for a single byte**: This test validates the function's behavior for minimal input.
 *    A single byte (0x01) is used to confirm that the function properly computes the checksum for very short data.
 * 
 * 4. **CRC-16 for daytime message**: This test checks the CRC calculation for a specific datetime string.
 *    The string "2025-03-30 10:27:06.500" is used, and the expected CRC is computed.
 */

#define CATCH_CONFIG_MAIN // This defines the main() function for Catch2

#include "../catch2/catch.hpp"
#include "CRC16_standalone.h"
#include <cstring>

/*! 
 * @struct TestCase
 * @brief Represents a single test case for CRC-16 checksum calculation.
 * 
 * This structure is used to define the input data, expected output, and description
 * for each test case in the unit tests for the CRC-16 checksum calculation function.
 * 
 * @var TestCase::data
 * Pointer to the input data for the CRC-16 calculation.
 * 
 * @var TestCase::length
 * The length of the input data in bytes.
 * 
 * @var TestCase::expectedCRC
 * The expected CRC-16 checksum value for the given input data.
 * 
 * @var TestCase::description
 * A brief description of the test case, used for documentation and debugging purposes.
 */
struct TestCase {
    const uint8_t* data;      /*!< Pointer to the input data for the CRC-16 calculation. */
    size_t length;            /*!< Length of the input data in bytes. */
    uint16_t expectedCRC;     /*!< Expected CRC-16 checksum value. */
    const char* description;  /*!< Description of the test case. */
};

TEST_CASE("calculateCRC16 computes correct CRC-16 checksum", "[CRC16]") {
    TestCase testCases[] = {
        {reinterpret_cast<const uint8_t*>("\x31\x32\x33\x34\x35"), 5, 0x4560, "CRC-16 for ASCII '12345'"},
        {reinterpret_cast<const uint8_t*>(""), 0, 0xFFFF, "CRC-16 for empty data"},
        {reinterpret_cast<const uint8_t*>("\x01"), 1, 0xF1D1, "CRC-16 for single byte"}
    };

    for (const auto& testCase : testCases) {
        SECTION(testCase.description) {
            REQUIRE(calculateCRC16(testCase.data, testCase.length) == testCase.expectedCRC);
        }
    }
}

TEST_CASE("CRC-16 for daytime message", "[CRC16]") {
    TestCase testCases[] = {
        {reinterpret_cast<const uint8_t*>("\x32\x30\x32\x35\x2D\x30\x33\x2D\x33\x30\x20\x31\x30\x3A\x32\x37\x3A\x30\x36\x2E\x35\x30\x30"),
         23, 0x4597, "CRC-16 for ASCII '2025-03-30 10:27:06.500'"}
    };

    for (const auto& testCase : testCases) {
        SECTION(testCase.description) {
            REQUIRE(calculateCRC16(testCase.data, testCase.length) == testCase.expectedCRC);
        }
    }
}

TEST_CASE("CRC-16 for binary data patterns", "[CRC16]") {
    SECTION("All zeros") {
        uint8_t zeros[10] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        REQUIRE(calculateCRC16(zeros, 10) == 0xE139);
    }

    SECTION("All ones") {
        uint8_t ones[10] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        REQUIRE(calculateCRC16(ones, 10) == 0xA6E1);
    }

    SECTION("Incremental pattern 0x00-0x0F") {
        uint8_t incremental[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
        REQUIRE(calculateCRC16(incremental, 16) == 0x3B37);
    }

    SECTION("Alternating pattern 0xAA") {
        uint8_t alternating[8] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
        REQUIRE(calculateCRC16(alternating, 8) == 0x059F);
    }

    SECTION("Alternating pattern 0x55") {
        uint8_t alternating55[8] = {0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55};
        REQUIRE(calculateCRC16(alternating55, 8) == 0xA37E);
    }
}

TEST_CASE("CRC-16 for NMEA sentences", "[CRC16][NMEA]") {
    SECTION("GGA sentence without checksum") {
        const char* gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,";
        REQUIRE(calculateCRC16(reinterpret_cast<const uint8_t*>(gga), strlen(gga)) == 0xF43B);
    }

    SECTION("RMC sentence without checksum") {
        const char* rmc = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W";
        REQUIRE(calculateCRC16(reinterpret_cast<const uint8_t*>(rmc), strlen(rmc)) == 0x3EF4);
    }

    SECTION("VTG sentence without checksum") {
        const char* vtg = "$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K";
        REQUIRE(calculateCRC16(reinterpret_cast<const uint8_t*>(vtg), strlen(vtg)) == 0xB7E1);
    }
}

TEST_CASE("CRC-16 for RTCM3 data simulation", "[CRC16][RTCM]") {
    SECTION("RTCM header simulation") {
        uint8_t rtcm_header[6] = {0xD3, 0x00, 0x13, 0x3E, 0xD7, 0xD3};
        REQUIRE(calculateCRC16(rtcm_header, 6) == 0x4D35);
    }

    SECTION("RTCM message type 1005 simulation") {
        uint8_t rtcm_1005[8] = {0xD3, 0x00, 0x13, 0x3E, 0xD0, 0x00, 0x03, 0xED};
        REQUIRE(calculateCRC16(rtcm_1005, 8) == 0x095F);
    }
}

TEST_CASE("CRC-16 edge cases and boundary conditions", "[CRC16][edge]") {
    SECTION("Maximum single byte value 0xFF") {
        uint8_t max_byte[1] = {0xFF};
        REQUIRE(calculateCRC16(max_byte, 1) == 0xFF00);
    }

    SECTION("Minimum non-zero byte 0x01") {
        uint8_t min_byte[1] = {0x01};
        REQUIRE(calculateCRC16(min_byte, 1) == 0xF1D1);
    }

    SECTION("Two bytes: 0x00 0x01") {
        uint8_t two_bytes[2] = {0x00, 0x01};
        REQUIRE(calculateCRC16(two_bytes, 2) == 0x0D2E);
    }

    SECTION("Large data block (256 bytes of 0xA5)") {
        uint8_t large_block[256];
        for (int i = 0; i < 256; i++) {
            large_block[i] = 0xA5;
        }
        REQUIRE(calculateCRC16(large_block, 256) == 0xA33A);
    }
}

TEST_CASE("CRC-16 for telemetry frame examples", "[CRC16][telemetry]") {
    SECTION("Telemetry frame with GPS coordinates") {
        // Simulated binary telemetry data: lat, lon, alt (double precision placeholders)
        uint8_t telemetry[24] = {
            0x40, 0x48, 0x0F, 0x5C, 0x28, 0xF5, 0xC2, 0x8F,  // Latitude
            0x40, 0x27, 0x08, 0x42, 0x99, 0x99, 0x99, 0x9A,  // Longitude
            0x40, 0x90, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00   // Altitude
        };
        REQUIRE(calculateCRC16(telemetry, 24) == 0x56CD);
    }

    SECTION("Status byte with timestamp") {
        uint8_t status_data[5] = {0x01, 0x5F, 0x8A, 0xBE, 0xCD}; // Status + 4-byte timestamp
        REQUIRE(calculateCRC16(status_data, 5) == 0xD3F0);
    }
}

TEST_CASE("CRC-16 consistency check", "[CRC16][consistency]") {
    SECTION("Same data produces same CRC") {
        uint8_t data[5] = {0x12, 0x34, 0x56, 0x78, 0x9A};
        uint16_t crc1 = calculateCRC16(data, 5);
        uint16_t crc2 = calculateCRC16(data, 5);
        REQUIRE(crc1 == crc2);
    }

    SECTION("Different data produces different CRC") {
        uint8_t data1[5] = {0x12, 0x34, 0x56, 0x78, 0x9A};
        uint8_t data2[5] = {0x12, 0x34, 0x56, 0x78, 0x9B};
        uint16_t crc1 = calculateCRC16(data1, 5);
        uint16_t crc2 = calculateCRC16(data2, 5);
        REQUIRE(crc1 != crc2);
    }

    SECTION("Order matters - byte swap changes CRC") {
        uint8_t data1[2] = {0x12, 0x34};
        uint8_t data2[2] = {0x34, 0x12};
        uint16_t crc1 = calculateCRC16(data1, 2);
        uint16_t crc2 = calculateCRC16(data2, 2);
        REQUIRE(crc1 != crc2);
    }
}
