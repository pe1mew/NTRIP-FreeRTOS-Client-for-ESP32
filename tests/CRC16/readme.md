# CRC16 Unit Tests with Catch2

This directory contains comprehensive unit tests for the CRC-16/CCITT-FALSE checksum calculation used throughout the espressiveFreeRTOS project.

## Purpose

The CRC-16 checksum is used for data integrity verification in:
- **Telemetry frames** sent via UART to external systems
- **Binary framing protocol** with byte stuffing
- **Data validation** for GPS and RTCM corrections
- **Day-time-stamp communication** to the acquisition unit

## Algorithm Specification

**CRC-16/CCITT-FALSE** (also known as CRC-16/AUTOSAR, CRC-16/IBM-3740):
- **Width:** 16 bits
- **Polynomial:** 0x1021
- **Initial Value:** 0xFFFF
- **Reflect Input:** No
- **Reflect Output:** No
- **Final XOR:** 0x0000

## Setup Instructions for Code::Blocks

### 1. Download Catch2 Header

Download the single-header version of Catch2 (v2.13.10):
```
https://github.com/catchorg/Catch2/releases/download/v2.13.10/catch.hpp
```

Place the downloaded `catch.hpp` file in: `tests/catch2/catch.hpp`

### 2. Open the Project

1. Open Code::Blocks
2. Go to **File → Open** and select `CRC16_Tests.cbp`
3. The project should load with three files:
   - `main.cpp` (test cases)
   - `CRC16_standalone.cpp` (implementation)
   - `CRC16_standalone.h` (header)

### 3. Build and Run

1. Select **Build → Build** (or press F9)
2. Select **Build → Run** (or press Ctrl+F10)
3. The test results will appear in the console

## Test Coverage

The test suite includes 10 test categories with 26+ individual assertions:

### 1. Basic CRC Checksum Tests
- ✓ ASCII string '12345' → CRC 0x4560
- ✓ Empty data → CRC 0xFFFF (initial value)
- ✓ Single byte 0x01 → CRC 0xF1D1

### 2. Datetime Message Tests
- ✓ ISO timestamp '2025-03-30 10:27:06.500' → CRC 0x4597

### 3. Binary Data Patterns
- ✓ All zeros (10 bytes) → CRC 0x9C58
- ✓ All ones (10 bytes) → CRC 0x2D6A
- ✓ Incremental 0x00-0x0F → CRC 0x3CAB
- ✓ Alternating pattern 0xAA → CRC 0x8F8E
- ✓ Alternating pattern 0x55 → CRC 0x577D

### 4. NMEA Sentence Validation
Tests CRC for GPS NMEA sentences (without checksums):
- ✓ GGA sentence (position) → CRC 0xE41F
- ✓ RMC sentence (date/time) → CRC 0x7C5E
- ✓ VTG sentence (speed/direction) → CRC 0x1D24

### 5. RTCM3 Data Simulation
Tests CRC for RTCM correction data:
- ✓ RTCM header (6 bytes) → CRC 0x4A6E
- ✓ RTCM message type 1005 (8 bytes) → CRC 0x20C6

### 6. Edge Cases and Boundary Conditions
- ✓ Maximum byte 0xFF → CRC 0xFF00
- ✓ Minimum byte 0x01 → CRC 0xF1D1
- ✓ Two bytes 0x00 0x01 → CRC 0x11BF
- ✓ Large block (256 bytes of 0xA5) → CRC 0x3F31

### 7. Telemetry Frame Examples
Simulated binary telemetry data:
- ✓ GPS coordinates (24 bytes) → CRC 0xA2F3
- ✓ Status + timestamp (5 bytes) → CRC 0x6E8E

### 8. Consistency Checks
- ✓ Same data produces same CRC
- ✓ Different data produces different CRC
- ✓ Byte order matters (0x12 0x34 ≠ 0x34 0x12)

## Compiler Requirements

- **MinGW/GCC**: Requires C++11 support (`-std=c++11`)
- The project is configured for both Debug and Release builds

## Verification

All expected CRC values can be verified using online calculators:
- **CRC Calculator:** https://crccalc.com/

**Settings for verification:**
1. Select "CRC-16/CCITT-FALSE" from the dropdown
2. Or manually configure:
   - Polynomial: 0x1021
   - Init: 0xFFFF
   - RefIn: false
   - RefOut: false
   - XorOut: 0x0000
3. Enter your test data
4. Compare with expected values in tests

## Example Test Data

**Test 1: ASCII '12345'**
```
Input:  31 32 33 34 35
Output: 0x4560
```

**Test 2: Datetime String**
```
Input:  "2025-03-30 10:27:06.500"
Hex:    32 30 32 35 2D 30 33 2D 33 30 20 31 30 3A 32 37 3A 30 36 2E 35 30 30
Output: 0x4597
```

**Test 3: All Zeros**
```
Input:  00 00 00 00 00 00 00 00 00 00
Output: 0x9C58
```

## Running Tests from Command Line

If you prefer command line compilation:

```bash
cd tests/CRC16
g++ -std=c++11 -Wall -o CRC16_Tests.exe CRC16_standalone.cpp main.cpp
CRC16_Tests.exe
```

## Expected Output

When all tests pass:
```
All tests passed (26 assertions in 10 test cases)
```

Example of successful test run:
```
===============================================================================
All tests passed (26 assertions in 10 test cases)

test cases: 10 | 10 passed
assertions: 26 | 26 passed
```

## Integration with Main Project

The test implementation (`CRC16_standalone.cpp`) is a copy of the main project file:
- **Source:** `src/lib/CRC16.cpp`
- **Header:** `src/lib/CRC16.h`

**Important:** After modifying `src/lib/CRC16.cpp`, update `CRC16_standalone.cpp` to keep tests synchronized.

## File Structure

```
CRC16/
├── main.cpp                    # Test cases (this is the test file)
├── CRC16_standalone.cpp        # Implementation copy from src/lib/
├── CRC16_standalone.h          # Header for standalone implementation
├── CRC16_Tests.cbp            # Code::Blocks project file
└── readme.md                   # This file
```

## Troubleshooting

### Error: "catch.hpp: No such file or directory"
- Download `catch.hpp` from https://github.com/catchorg/Catch2/releases/download/v2.13.10/catch.hpp
- Place it in `tests/catch2/catch.hpp` (one level up from this directory)

### Test failures
- Verify expected values using https://crccalc.com/ with CRC-16/CCITT-FALSE
- Ensure `CRC16_standalone.cpp` matches `src/lib/CRC16.cpp`
- Check that test data is entered correctly (hex values)

### Compilation errors
- Ensure C++11 is enabled (`-std=c++11`)
- Check that all three files are included in the project
- Verify `#include <cstring>` is present in `main.cpp` for `strlen()`

## Adding New Tests

To add new test cases:

```cpp
TEST_CASE("Your test description", "[CRC16][tag]") {
    SECTION("Specific test scenario") {
        uint8_t data[] = {0x01, 0x02, 0x03};
        REQUIRE(calculateCRC16(data, 3) == 0xYOURCRC);
    }
}
```

**Tips:**
1. Use descriptive names and tags
2. Calculate expected CRC using https://crccalc.com/
3. Include both typical and edge case data
4. Document the purpose of complex test data
5. Group related tests using `SECTION` macros

## References

- **Catch2 Documentation:** https://github.com/catchorg/Catch2
- **CRC Calculator:** https://crccalc.com/
- **CRC Catalogue:** https://reveng.sourceforge.io/crc-catalogue/
- **Main Implementation:** `src/lib/CRC16.cpp` 