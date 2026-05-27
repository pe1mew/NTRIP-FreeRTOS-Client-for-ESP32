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

#include "esp_log.h"
#include "mbedtls/base64.h"
#include <cstring>
#include <cstdio>
#include <cstddef>

/**
 * @class NTRIPClient
 * @brief A client for NTRIP (Networked Transport of RTCM via Internet Protocol).
 *
 * Uses a raw lwIP TCP socket (not esp_http_client) because NTRIP needs
 * bidirectional traffic on a single socket: the client streams GGA upstream
 * while the caster streams RTCM downstream. esp_http_client's request/response
 * state machine RSTs the socket when it sees unsolicited writes after a GET
 * response begins, breaking NTRIP 2.0; without the Ntrip/2.0 header the
 * caster falls back to NTRIP 1.0 "ICY 200 OK", which esp_http_client_fetch_headers
 * can't parse. Raw sockets handle both response shapes and full duplex.
 */
class NTRIPClient {
private:
    int sock_fd;        // raw TCP socket; -1 when not connected
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
