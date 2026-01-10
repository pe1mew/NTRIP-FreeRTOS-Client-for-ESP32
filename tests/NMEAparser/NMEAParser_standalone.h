#ifndef NMEAPARSER_STANDALONE_H
#define NMEAPARSER_STANDALONE_H

// Structures for NMEA data
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
    double speed;
    double direction;
};

// Function declarations
GGAData parseGGASentence(const char* ggaSentence);
RMCData parseRMCSentence(const char* rmcSentence);
VTGData parseVTGSentence(const char* vtgSentence);

#endif // NMEAPARSER_STANDALONE_H
