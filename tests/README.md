# Unit Tests for espressiveFreeRTOS

This directory contains unit tests for various modules of the espressiveFreeRTOS project using the Catch2 testing framework and Code::Blocks compiler.

## Directory Structure

```
tests/
├── catch2/              # Catch2 single-header library
│   └── catch.hpp       # Download from Catch2 releases
├── NMEAparser/         # NMEA sentence parsing tests
│   ├── test_NMEAParser.cpp
│   ├── NMEAParser_standalone.cpp/h
│   ├── NMEAParser_Tests.cbp
│   └── README.md
├── CRC16/              # CRC-16/CCITT-FALSE checksum tests
│   ├── main.cpp
│   ├── CRC16_standalone.cpp/h
│   └── CRC16_Tests.cbp
└── .gitignore          # Excludes build artifacts
```

## Prerequisites

### 1. Install Code::Blocks

Download and install Code::Blocks with MinGW:
- https://www.codeblocks.org/downloads/

Ensure you have the version that includes the MinGW compiler.

### 2. Download Catch2 Header

Download the single-header version of Catch2 (v2.13.10):
```
https://github.com/catchorg/Catch2/releases/download/v2.13.10/catch.hpp
```

**Important:** Place the downloaded `catch.hpp` file in: `tests/catch2/catch.hpp`

This single file is required by all test projects.

## Running Tests

### Option 1: Using Code::Blocks IDE

1. Open Code::Blocks
2. Go to **File → Open** and select the `.cbp` project file:
   - `NMEAparser/NMEAParser_Tests.cbp` for NMEA tests
   - `CRC16/CRC16_Tests.cbp` for CRC16 tests
3. Select **Build → Build** (F9)
4. Select **Build → Run** (Ctrl+F10)
5. View test results in the console

### Option 2: Command Line Compilation

Navigate to the test directory and compile:

**For NMEAParser tests:**
```bash
cd tests/NMEAparser
g++ -std=c++11 -Wall -o NMEAParser_Tests.exe NMEAParser_standalone.cpp test_NMEAParser.cpp
NMEAParser_Tests.exe
```

**For CRC16 tests:**
```bash
cd tests/CRC16
g++ -std=c++11 -Wall -o CRC16_Tests.exe CRC16_standalone.cpp main.cpp
CRC16_Tests.exe
```

## Test Modules

### 1. NMEAParser Tests

Tests NMEA sentence parsing for GPS data.

**Test Coverage:**
- ✓ GGA sentences (position, altitude, fix quality)
- ✓ RMC sentences (date/time, validity)
- ✓ VTG sentences (speed, direction)
- ✓ Coordinate conversion (NMEA → decimal degrees)
- ✓ Edge cases (empty, malformed data)
- ✓ Y2K date handling (1980-2079)

**Total:** 19 test cases with 35+ assertions

**See:** [NMEAparser/README.md](NMEAparser/README.md) for detailed documentation

### 2. CRC16 Tests

Tests CRC-16/CCITT-FALSE checksum calculation used for data integrity.

**Test Coverage:**
- ✓ Basic data patterns (zeros, ones, incremental)
- ✓ ASCII strings and datetime messages
- ✓ NMEA sentence validation (GGA, RMC, VTG)
- ✓ RTCM3 data simulation
- ✓ Telemetry frame checksums
- ✓ Edge cases (empty, single byte, 256 bytes)
- ✓ Consistency checks (same data → same CRC)

**Algorithm Details:**
- **Name:** CRC-16/CCITT-FALSE (also known as CRC-16/AUTOSAR, CRC-16/IBM-3740)
- **Width:** 16 bits
- **Polynomial:** 0x1021
- **Initial Value:** 0xFFFF
- **Reflect Input:** No
- **Reflect Output:** No
- **Final XOR:** 0x0000

**Total:** 10+ test cases with 26+ assertions

**Verification:** Use https://crccalc.com/ to verify expected values

## Expected Test Output

When all tests pass, you should see:
```
All tests passed (XX assertions in YY test cases)
```

If any test fails, Catch2 provides detailed information:
- Test case name
- Expected vs actual values
- File and line number of the failure

## Compiler Requirements

- **C++ Standard:** C++11 or higher (`-std=c++11`)
- **Compiler:** GCC/MinGW (tested with MinGW-w64)
- **Warnings:** Compiled with `-Wall` for maximum code quality

## Build Artifacts

The `.gitignore` file excludes:
- `bin/` and `obj/` directories (build outputs)
- `*.exe`, `*.o`, `*.obj` (executables and object files)
- `*.layout`, `*.depend` (Code::Blocks IDE files)
- `*.bak`, `*.save` (backup files)

## Integration with Main Project

These tests use **standalone implementations** of the code:
- `NMEAParser_standalone.cpp` is a copy of `src/NMEAparser/NMEAParser.cpp`
- `CRC16_standalone.cpp` is a copy of `src/lib/CRC16.cpp`

This approach ensures:
1. Tests compile independently without ESP-IDF dependencies
2. Tests can run on any platform with a C++11 compiler
3. No conflicts with FreeRTOS or ESP32-specific code
4. Fast iteration during development

**Important:** After modifying the main source files, update the corresponding standalone test implementations to keep them synchronized.

## Troubleshooting

### Error: "catch.hpp: No such file or directory"
- Download `catch.hpp` from the link above
- Place it in `tests/catch2/catch.hpp`
- Ensure the path is correct relative to test files

### Error: "NMEAParser_standalone.h: No such file or directory"
- Ensure you opened the correct `.cbp` project file
- Check that all source files are in the same directory as the project file

### Linking errors
- Ensure both `.cpp` files are included in the Code::Blocks project
- Check that the compiler supports C++11 (`-std=c++11`)

### Tests compile but fail
- Verify expected CRC values using https://crccalc.com/
- Check that standalone implementations match the main source files
- Review test output for specific assertion failures

## Contributing

When adding new tests:
1. Follow the existing test structure using Catch2 `TEST_CASE` and `SECTION` macros
2. Include descriptive test names and tags (e.g., `[CRC16][edge]`)
3. Add comprehensive test coverage for edge cases
4. Update this README with new test information
5. Ensure tests pass before committing

## References

- **Catch2 Documentation:** https://github.com/catchorg/Catch2
- **CRC Calculator:** https://crccalc.com/
- **NMEA Specification:** https://www.nmea.org/
- **Code::Blocks:** https://www.codeblocks.org/
