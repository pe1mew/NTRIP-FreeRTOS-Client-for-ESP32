#include "NMEAParser.h"
#include <cstring>
#include <cstdlib>

GGAData parseGGASentence(const char* ggaSentence) {
    GGAData data = {};
    char sentenceCopy[256];
    strncpy(sentenceCopy, ggaSentence, sizeof(sentenceCopy) - 1);
    sentenceCopy[sizeof(sentenceCopy) - 1] = '\0';

    char* token = strtok(sentenceCopy, ",");
    int fieldIndex = 0;

    while (token != NULL) {
        switch (fieldIndex) {
            case 1: // Time
                strncpy(data.timeBuffer, token, sizeof(data.timeBuffer) - 1);
                break;
            case 2: // Latitude
                data.latitude = atof(token);
                break;
            case 3: // Latitude direction (N/S)
                data.latDirection = token[0];
                break;
            case 4: // Longitude
                data.longitude = atof(token);
                break;
            case 5: // Longitude direction (E/W)
                data.lonDirection = token[0];
                break;
            case 6: // Fix type
                data.fixType = atoi(token);
                break;
            case 7: // Satellites
                data.satellites = atoi(token);
                break;
            case 8: // HDOP
                data.hdop = atof(token);
                break;
            case 9: // Altitude
                data.altitude = atof(token);
                break;
            case 13: // Age of Differential Data
                data.ageOfDifferentialData = atof(token);
                break;
        }
        token = strtok(NULL, ",");
        fieldIndex++;
    }

    // Convert latitude and longitude to decimal degrees
    int latDegrees = (int)(data.latitude / 100);
    double latMinutes = data.latitude - (latDegrees * 100);
    data.latitude = latDegrees + (latMinutes / 60.0);
    if (data.latDirection == 'S') {
        data.latitude = -data.latitude; // South is negative
    }

    int lonDegrees = (int)(data.longitude / 100);
    double lonMinutes = data.longitude - (lonDegrees * 100);
    data.longitude = lonDegrees + (lonMinutes / 60.0);
    if (data.lonDirection == 'W') {
        data.longitude = -data.longitude; // West is negative
    }

    return data;
}

RMCData parseRMCSentence(const char* rmcSentence) {
    RMCData data = {};
    data.year = 2025;  // Default values
    data.month = 1;
    data.day = 1;
    data.valid = false;
    
    char sentenceCopy[256];
    strncpy(sentenceCopy, rmcSentence, sizeof(sentenceCopy) - 1);
    sentenceCopy[sizeof(sentenceCopy) - 1] = '\0';

    char* token = strtok(sentenceCopy, ",");
    int fieldIndex = 0;
    char dateBuffer[7] = {0};

    while (token != NULL) {
        switch (fieldIndex) {
            case 2: // Status
                if (strcmp(token, "A") == 0) {
                    data.valid = true; // Valid date format
                }
                break;
            case 9: // Date
                strncpy(dateBuffer, token, sizeof(dateBuffer) - 1);
                break;
        }
        token = strtok(NULL, ",");
        fieldIndex++;
    }

    // Parse date if valid format (DDMMYY)
    if (strlen(dateBuffer) == 6) {
        data.day = (dateBuffer[0] - '0') * 10 + (dateBuffer[1] - '0');
        data.month = (dateBuffer[2] - '0') * 10 + (dateBuffer[3] - '0');
        int yy = (dateBuffer[4] - '0') * 10 + (dateBuffer[5] - '0');
        // Y2K handling: 80-99 = 1980-1999, 00-79 = 2000-2079
        data.year = (yy >= 80) ? (1900 + yy) : (2000 + yy);
    }

    return data;
}

VTGData parseVTGSentence(const char* vtgSentence) {
    VTGData data = {};
    data.speed = 0.0;
    data.direction = 0.0;
    
    char sentenceCopy[256];
    strncpy(sentenceCopy, vtgSentence, sizeof(sentenceCopy) - 1);
    sentenceCopy[sizeof(sentenceCopy) - 1] = '\0';

    char* token = strtok(sentenceCopy, ",");
    int fieldIndex = 0;
    char* previousToken = nullptr;

    while (token != NULL) {
        // Extract speed from VTG sentence.
        // Because VTG sentence may differ in format, we need to check token
        // to determine if the previous token is speed and in which format it is, km/h or m/s.
        // Check if the current token starts with 'K' (handles "K*48" checksum)
        // and the previous token is not null, then convert from km/h to m/s.
        if (token[0] == 'K' && previousToken != nullptr) {
            data.speed = atof(previousToken) / 3.6; // Convert from km/h to m/s
        }

        // Retrieve direction only if direction is "T" (true north)
        switch (fieldIndex) {
            case 1: // Direction (true north)
                data.direction = atof(token);
                break;
            case 2: // Validate 'T' field
                if (strcmp(token, "T") != 0) {
                    data.direction = 0.0; // Invalid direction
                }
                break;
        }

        // Update the previous token as part of speed retrieval and move to the next token
        previousToken = token;
        token = strtok(NULL, ",");
        fieldIndex++;
    }

    return data;
}