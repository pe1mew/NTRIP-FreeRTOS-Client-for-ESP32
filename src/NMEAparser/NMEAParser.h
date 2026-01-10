#ifndef NMEAPARSER_H
#define NMEAPARSER_H

#include <string>

struct GGAData {
    double latitude;
    double longitude;
    double altitude;
    int fixType;
    int satellites;
    double hdop;
    double ageOfDifferentialData;
    char latDirection;
    char lonDirection;
    char timeBuffer[11];
};

struct RMCData {
    int year;
    int month;
    int day;
    bool valid;
};

struct VTGData {
    double speed;      // Speed in m/s
    double direction;  // Direction in degrees (true north)
};

/**
 * @brief Parses a GGA sentence and extracts relevant information.
 * @param ggaSentence The GGA sentence to parse.
 * @return A struct containing the parsed GGA data.
 */
GGAData parseGGASentence(const char* ggaSentence);

/**
 * @brief Parses an RMC sentence and extracts date information.
 * @param rmcSentence The RMC sentence to parse.
 * @return A struct containing the parsed RMC data (date).
 */
RMCData parseRMCSentence(const char* rmcSentence);

/**
 * @brief Parses a VTG sentence and extracts speed and direction information.
 * @param vtgSentence The VTG sentence to parse.
 * @return A struct containing the parsed VTG data (speed and direction).
 */
VTGData parseVTGSentence(const char* vtgSentence);

#endif // NMEAPARSER_H