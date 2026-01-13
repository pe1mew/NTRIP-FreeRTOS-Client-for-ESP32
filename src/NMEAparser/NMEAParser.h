#ifndef NMEAPARSER_H
#define NMEAPARSER_H

#include <string>

/**
 * @brief Parsed data from a GGA NMEA sentence.
 */
struct GGAData {
    double latitude;                /**< Latitude in decimal degrees */
    double longitude;               /**< Longitude in decimal degrees */
    double altitude;                /**< Altitude in meters */
    int fixType;                    /**< GNSS fix type (0=no fix, 1=GPS, 2=DGPS, 4=RTK fixed, 5=RTK float) */
    int satellites;                 /**< Number of satellites used */
    double hdop;                    /**< Horizontal dilution of precision */
    double ageOfDifferentialData;   /**< Age of differential data (seconds) */
    char latDirection;              /**< Latitude direction ('N' or 'S') */
    char lonDirection;              /**< Longitude direction ('E' or 'W') */
    char timeBuffer[11];            /**< UTC time string (hhmmss.sss) */
};

/**
 * @brief Parsed date and validity from an RMC NMEA sentence.
 */
struct RMCData {
    int year;       /**< Year (YYYY) */
    int month;      /**< Month (1-12) */
    int day;        /**< Day (1-31) */
    bool valid;     /**< True if RMC sentence is valid */
};

/**
 * @brief Parsed speed and direction from a VTG NMEA sentence.
 */
struct VTGData {
    double speed;      /**< Speed in m/s */
    double direction;  /**< Direction in degrees (true north) */
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