# NMEAParser Unit Tests with Catch2

This directory contains unit tests for the NMEAParser module using the Catch2 testing framework.

## Setup Instructions for Code::Blocks

### 1. Download Catch2 Header

Download the single-header version of Catch2 (v2.13.10):
```
https://github.com/catchorg/Catch2/releases/download/v2.13.10/catch.hpp
```

Place the downloaded `catch.hpp` file in: `tests/catch2/catch.hpp`

### 2. Open the Project

1. Open Code::Blocks
2. Go to **File → Open** and select `NMEAParser_Tests.cbp`
3. The project should load with two source files:
   - `NMEAParser_standalone.cpp` (implementation)
   - `test_NMEAParser.cpp` (test cases)

### 3. Build and Run

1. Select **Build → Build** (or press F9)
2. Select **Build → Run** (or press Ctrl+F10)
3. The test results will appear in the console

## Test Coverage

The test suite covers:

### parseGGASentence Tests
- ✓ Valid GGA sentence parsing
- ✓ Time extraction
- ✓ Latitude/longitude conversion to decimal degrees
- ✓ Altitude, fix type, satellites, HDOP
- ✓ Southern and Western coordinates (negative values)
- ✓ RTK Fixed position (fix type 4)
- ✓ Age of differential data
- ✓ Empty or malformed sentences

### parseRMCSentence Tests
- ✓ Valid RMC sentence with date parsing
- ✓ Date format (DDMMYY) conversion
- ✓ Valid/invalid status detection
- ✓ Modern dates (2026)

### parseVTGSentence Tests
- ✓ Valid VTG sentence parsing
- ✓ Direction extraction (true north)
- ✓ Speed conversion from km/h to m/s
- ✓ Zero speed handling
- ✓ High speed scenarios
- ✓ Missing K indicator

## Compiler Requirements

- **MinGW/GCC**: Requires C++11 support (`-std=c++11`)
- The project is configured for both Debug and Release builds

## Test Examples

Sample NMEA sentences used in tests:

**GGA (Position):**
```
$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47
```

**RMC (Date/Time):**
```
$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A
```

**VTG (Speed/Direction):**
```
$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*48
```

## Running Tests from Command Line

If you prefer command line compilation:

```bash
cd tests
g++ -std=c++11 -Wall -o NMEAParser_Tests.exe NMEAParser_standalone.cpp test_NMEAParser.cpp
NMEAParser_Tests.exe
```

## Expected Output

When all tests pass, you should see:
```
All tests passed (XX assertions in YY test cases)
```

If any test fails, Catch2 will provide detailed information about:
- Which test case failed
- The expected vs actual values
- The line number where the assertion failed
