// --------------------------------------------------------------------
//   This file is part of the PE1MEW NTRIP Client.
//
//   The NTRIP Client is distributed in the hope that 
//   it will be useful, but WITHOUT ANY WARRANTY; without even the 
//   implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
//   PURPOSE.
// --------------------------------------------------------------------*/

/*!
   
 \file NTRIPClient.h
 \brief NTRIPClient class definition
 \author Remko Welling (PE1MEW) 

 This file was retrieved from the following work:
  - [NTRIP-client-for-Arduino](https://github.com/GLAY-AK2/NTRIP-client-for-Arduino)

 The file was refactored to be used in the PE1MEW NTRIP Client project.

*/

#ifndef NTRIP_CLIENT
#define NTRIP_CLIENT

#include "esp_http_client.h"
#include "esp_log.h"
#include "mbedtls/base64.h"
#include <cstring>
#include <cstdio>

/**
 * @class NTRIPClient
 * @brief A client for NTRIP (Networked Transport of RTCM via Internet Protocol).
 * 
 * This class provides functionality for requesting MountPoints List and RAW data
 * from an NTRIP Caster using ESP-IDF HTTP client.
 */
class NTRIPClient {
private:
    esp_http_client_handle_t client;
    char* buffer;
    size_t buffer_size;
    size_t buffer_pos;
    bool connected_flag;

    bool base64Encode(const char* input, char* output, size_t output_size);

public:
    NTRIPClient();
    ~NTRIPClient();

    /**
     * @brief Connect to NTRIP caster and initialize client.
     * @return true if initialization was successful, false otherwise.
     */
    bool init();
    /**
     * @brief Request the MountPoints List serviced by the NTRIP Caster without username and password.
     * 
     * @param[in] host The hostname of the NTRIP Caster.
     * @param[in] port The port number of the NTRIP Caster.
     * @return true if the request was successful, false otherwise.
     */
    bool reqSrcTblNoAuth(const char* host, int &port);

    /**
     * @brief Request the MountPoints List serviced by the NTRIP Caster with user authentication.
     * 
     * @param[in] host The hostname of the NTRIP Caster.
     * @param[in] port The port number of the NTRIP Caster.
     * @param[in] user The username for authentication.
     * @param[in] psw The password for authentication.
     * @return true if the request was successful, false otherwise.
     */
    bool reqSrcTbl(const char* host, int &port, const char* user, const char* psw);

    /**
     * @brief Request RAW data from the NTRIP Caster with user authentication.
     * 
     * @param[in] host The hostname of the NTRIP Caster.
     * @param[in] port The port number of the NTRIP Caster.
     * @param[in] mntpnt The MountPoint to request data from.
     * @param[in] user The username for authentication.
     * @param[in] psw The password for authentication.
     * @return true if the request was successful, false otherwise.
     */
    bool reqRaw(const char* host, int &port, const char* mntpnt, const char* user, const char* psw);

    /**
     * @brief Request RAW data from the NTRIP Caster without user authentication.
     * 
     * @param[in] host The hostname of the NTRIP Caster.
     * @param[in] port The port number of the NTRIP Caster.
     * @param[in] mntpnt The MountPoint to request data from.
     * @return true if the request was successful, false otherwise.
     */
    bool reqRaw(const char* host, int &port, const char* mntpnt);

    /**
     * @brief Read a line of data from the NTRIP Caster.
     * 
     * @param[out] buffer The buffer to store the read data.
     * @param[in] size The size of the buffer.
     * @return The number of bytes read.
     */
    int readLine(char* buffer, int size);

    /**
     * @brief Send a GGA sentence to the NTRIP Caster.
     * 
     * @param[in] gga The GGA sentence to send.
     */
    void sendGGA(const char* gga);

    /**
     * @brief Check if connected to NTRIP Caster.
     * @return true if connected, false otherwise.
     */
    bool isConnected();

    /**
     * @brief Disconnect from NTRIP Caster.
     */
    void disconnect();

    /**
     * @brief Read available data from the NTRIP stream.
     * @param[out] data Buffer to store read data.
     * @param[in] size Maximum bytes to read.
     * @return Number of bytes read.
     */
    int readData(uint8_t* data, size_t size);

    /**
     * @brief Check how many bytes are available to read.
     * @return Number of bytes available.
     */
    int available();
};

#endif
