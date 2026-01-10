#define CATCH_CONFIG_MAIN
#include "../catch2/catch.hpp"
#include "NMEAParser_standalone.h"
#include <cmath>

// Helper function to compare floating point values
bool almostEqual(double a, double b, double epsilon = 0.000001) {
    return std::fabs(a - b) < epsilon;
}

TEST_CASE("parseGGASentence - Valid GGA sentence", "[NMEAParser]") {
    const char* ggaSentence = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
    
    GGAData data = parseGGASentence(ggaSentence);
    
    SECTION("Time is extracted correctly") {
        REQUIRE(std::strcmp(data.timeBuffer, "123519") == 0);
    }
    
    SECTION("Latitude is converted to decimal degrees") {
        // 4807.038 N = 48 + 7.038/60 = 48.1173 degrees
        REQUIRE(almostEqual(data.latitude, 48.1173, 0.0001));
    }
    
    SECTION("Longitude is converted to decimal degrees") {
        // 01131.000 E = 11 + 31.000/60 = 11.516667 degrees
        REQUIRE(almostEqual(data.longitude, 11.516667, 0.0001));
    }
    
    SECTION("Altitude is extracted") {
        REQUIRE(almostEqual(data.altitude, 545.4, 0.01));
    }
    
    SECTION("Fix type is extracted") {
        REQUIRE(data.fixType == 1);
    }
    
    SECTION("Satellites count is extracted") {
        REQUIRE(data.satellites == 8);
    }
    
    SECTION("HDOP is extracted") {
        REQUIRE(almostEqual(data.hdop, 0.9, 0.01));
    }
    
    SECTION("Latitude direction is extracted") {
        REQUIRE(data.latDirection == 'N');
    }
    
    SECTION("Longitude direction is extracted") {
        REQUIRE(data.lonDirection == 'E');
    }
}

TEST_CASE("parseGGASentence - Southern and Western coordinates", "[NMEAParser]") {
    const char* ggaSentence = "$GPGGA,123519,3345.678,S,15112.345,W,1,05,1.2,100.0,M,10.0,M,,*47";
    
    GGAData data = parseGGASentence(ggaSentence);
    
    SECTION("Southern latitude is negative") {
        // 3345.678 S = -(33 + 45.678/60) = -33.7613 degrees
        REQUIRE(almostEqual(data.latitude, -33.7613, 0.0001));
        REQUIRE(data.latDirection == 'S');
    }
    
    SECTION("Western longitude is negative") {
        // 15112.345 W = -(151 + 12.345/60) = -151.205750 degrees
        REQUIRE(almostEqual(data.longitude, -151.205750, 0.0001));
        REQUIRE(data.lonDirection == 'W');
    }
}

TEST_CASE("parseGGASentence - RTK Fixed position", "[NMEAParser]") {
    const char* ggaSentence = "$GPGGA,123519,4807.038,N,01131.000,E,4,12,0.5,545.4,M,46.9,M,3.2,0001*47";
    
    GGAData data = parseGGASentence(ggaSentence);
    
    SECTION("RTK fix type is 4") {
        REQUIRE(data.fixType == 4);
    }
    
    SECTION("Age of differential data is extracted") {
        REQUIRE(almostEqual(data.ageOfDifferentialData, 3.2, 0.01));
    }
    
    SECTION("Satellite count for RTK") {
        REQUIRE(data.satellites == 12);
    }
    
    SECTION("HDOP for RTK fix") {
        REQUIRE(almostEqual(data.hdop, 0.5, 0.01));
    }
}

TEST_CASE("parseRMCSentence - Valid RMC sentence with date", "[NMEAParser]") {
    const char* rmcSentence = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
    
    RMCData data = parseRMCSentence(rmcSentence);
    
    SECTION("Date is parsed correctly") {
        REQUIRE(data.day == 23);
        REQUIRE(data.month == 3);
        REQUIRE(data.year == 1994);
    }
    
    SECTION("Valid status is set") {
        REQUIRE(data.valid == true);
    }
}

TEST_CASE("parseRMCSentence - Invalid RMC sentence", "[NMEAParser]") {
    const char* rmcSentence = "$GPRMC,123519,V,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
    
    RMCData data = parseRMCSentence(rmcSentence);
    
    SECTION("Invalid status is detected") {
        REQUIRE(data.valid == false);
    }
}

TEST_CASE("parseRMCSentence - Modern date", "[NMEAParser]") {
    const char* rmcSentence = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,100126,003.1,W*6A";
    
    RMCData data = parseRMCSentence(rmcSentence);
    
    SECTION("Year 2026 date is parsed correctly") {
        REQUIRE(data.day == 10);
        REQUIRE(data.month == 1);
        REQUIRE(data.year == 2026);
    }
}

TEST_CASE("parseVTGSentence - Valid VTG sentence", "[NMEAParser]") {
    const char* vtgSentence = "$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*48";
    
    VTGData data = parseVTGSentence(vtgSentence);
    
    SECTION("Direction is extracted") {
        REQUIRE(almostEqual(data.direction, 54.7, 0.1));
    }
    
    SECTION("Speed is converted from km/h to m/s") {
        // 10.2 km/h = 10.2 / 3.6 = 2.833 m/s
        REQUIRE(almostEqual(data.speed, 2.833, 0.01));
    }
}

TEST_CASE("parseVTGSentence - Zero speed", "[NMEAParser]") {
    const char* vtgSentence = "$GPVTG,0.0,T,0.0,M,0.0,N,0.0,K*48";
    
    VTGData data = parseVTGSentence(vtgSentence);
    
    SECTION("Zero speed is handled") {
        REQUIRE(almostEqual(data.speed, 0.0, 0.01));
    }
    
    SECTION("Zero direction is handled") {
        REQUIRE(almostEqual(data.direction, 0.0, 0.01));
    }
}

TEST_CASE("parseVTGSentence - High speed", "[NMEAParser]") {
    const char* vtgSentence = "$GPVTG,234.5,T,234.5,M,65.2,N,120.8,K*48";
    
    VTGData data = parseVTGSentence(vtgSentence);
    
    SECTION("High speed is converted correctly") {
        // 120.8 km/h = 120.8 / 3.6 = 33.556 m/s
        REQUIRE(almostEqual(data.speed, 33.556, 0.01));
    }
    
    SECTION("Direction over 180 degrees") {
        REQUIRE(almostEqual(data.direction, 234.5, 0.1));
    }
}

TEST_CASE("parseGGASentence - Empty or malformed sentence", "[NMEAParser]") {
    SECTION("Empty string") {
        const char* empty = "";
        GGAData data = parseGGASentence(empty);
        // Should return zero-initialized structure
        REQUIRE(data.fixType == 0);
    }
    
    SECTION("Sentence with missing fields") {
        const char* incomplete = "$GPGGA,123519,4807.038,N";
        GGAData data = parseGGASentence(incomplete);
        // Should handle gracefully - some fields may be zero
        REQUIRE(data.latDirection == 'N');
    }
}

TEST_CASE("parseVTGSentence - Sentence without K indicator", "[NMEAParser]") {
    const char* vtgSentence = "$GPVTG,054.7,T,034.4,M,005.5,N*48";
    
    VTGData data = parseVTGSentence(vtgSentence);
    
    SECTION("Direction is still extracted") {
        REQUIRE(almostEqual(data.direction, 54.7, 0.1));
    }
    
    SECTION("Speed defaults to zero without K") {
        REQUIRE(almostEqual(data.speed, 0.0, 0.01));
    }
}
