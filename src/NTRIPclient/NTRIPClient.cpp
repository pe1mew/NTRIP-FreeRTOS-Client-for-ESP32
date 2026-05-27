// --------------------------------------------------------------------
//   This file is part of the PE1MEW NTRIP Client.
//
//   The NTRIP Client is distributed in the hope that
//   it will be useful, but WITHOUT ANY WARRANTY; without even the
//   implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//   PURPOSE.
// --------------------------------------------------------------------*/

/*!
 \file NTRIPClient.cpp
 \brief NTRIPClient class implementation (raw lwIP socket transport)
 \author Remko Welling (PE1MEW)

 This implementation uses a raw TCP socket via lwIP / BSD sockets rather than
 esp_http_client. NTRIP requires bidirectional traffic on a single socket
 (client sends GGA, caster sends RTCM) which esp_http_client's request/response
 state machine cannot model:
   - With "Ntrip-Version: Ntrip/2.0" set, the caster speaks HTTP/1.1; the first
     inline GGA after the GET response begins is treated as a protocol violation
     and the caster sends TCP RST (errno 104 Connection reset by peer).
   - Without that header, the caster falls back to NTRIP/1.0 and responds with
     "ICY 200 OK\r\n" which esp_http_client_fetch_headers() cannot parse,
     yielding HTTP Status = -1 and a connect failure.
 Raw sockets handle both response shapes ("HTTP/1.x NNN ..." and "ICY 200 OK")
 and allow real full-duplex I/O for the streaming phase.
*/

#include "NTRIPClient.h"
#include "../statisticsTask.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <cstring>
#include <cstdio>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>

static const char* TAG = "NTRIPClient";

// Timeout (seconds) for the initial connect + status-line read.
static const int NTRIP_CONNECT_TIMEOUT_SEC = 30;
// Read timeout for the streaming phase. Keep short so the task loop can
// service the GGA queue promptly between RTCM reads.
static const int NTRIP_STREAM_READ_TIMEOUT_MS = 100;

NTRIPClient::NTRIPClient()
    : sock_fd(-1), buffer(nullptr), buffer_size(2048),
      buffer_pos(0), connected_flag(false) {
    buffer = new char[buffer_size];
}

NTRIPClient::~NTRIPClient() {
    disconnect();
    if (buffer) {
        delete[] buffer;
        buffer = nullptr;
    }
}

bool NTRIPClient::init() {
    return true;
}

bool NTRIPClient::base64Encode(const char* input, char* output, size_t output_size) {
    size_t olen = 0;
    int ret = mbedtls_base64_encode(
        (unsigned char*)output, output_size, &olen,
        (const unsigned char*)input, strlen(input)
    );
    if (ret == 0 && olen < output_size) {
        output[olen] = '\0';
        return true;
    }
    return false;
}

// ---- Internal helpers ------------------------------------------------------

// Set both SO_RCVTIMEO and SO_SNDTIMEO on the given socket.
static void set_socket_timeout_ms(int fd, int ms) {
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

// Read exactly one CR/LF-terminated line into `line` (excluding terminator).
// Returns the line length, or -1 on error/timeout/EOF.
static int read_line(int fd, char* line, size_t max_len) {
    size_t idx = 0;
    while (idx + 1 < max_len) {
        char c;
        int n = recv(fd, &c, 1, 0);
        if (n <= 0) {
            return -1;
        }
        if (c == '\n') {
            // Strip trailing \r if present
            if (idx > 0 && line[idx - 1] == '\r') {
                idx--;
            }
            line[idx] = '\0';
            return (int)idx;
        }
        line[idx++] = c;
    }
    line[max_len - 1] = '\0';
    return -1;  // line too long
}

// ---- Connect / handshake ---------------------------------------------------

bool NTRIPClient::reqRaw(const char* host, int &port, const char* mntpnt,
                         const char* user, const char* psw) {
    // Safety: tear down any previous connection
    disconnect();

    ESP_LOGI(TAG, "NTRIP connect: %s:%d /%s", host, port, mntpnt);

    // Build Authorization header if credentials provided
    char auth_header[300] = "";
    if (user && strlen(user) > 0) {
        char auth_input[128];
        snprintf(auth_input, sizeof(auth_input), "%s:%s", user, psw ? psw : "");
        char auth_encoded[256];
        if (!base64Encode(auth_input, auth_encoded, sizeof(auth_encoded))) {
            ESP_LOGE(TAG, "Failed to base64-encode credentials");
            return false;
        }
        snprintf(auth_header, sizeof(auth_header),
                 "Authorization: Basic %s\r\n", auth_encoded);
    }

    // Build NTRIP GET request. We deliberately do NOT include Ntrip-Version:
    // see the file header for why. HTTP/1.0 keeps caster-side framing simple.
    char request[1024];
    int req_len = snprintf(request, sizeof(request),
        "GET /%s HTTP/1.0\r\n"
        "User-Agent: NTRIP NTRIPClient ESP32 v1.0\r\n"
        "Accept: */*\r\n"
        "%s"
        "\r\n",
        mntpnt ? mntpnt : "", auth_header);
    if (req_len < 0 || req_len >= (int)sizeof(request)) {
        ESP_LOGE(TAG, "Request truncated (len=%d)", req_len);
        return false;
    }

    // DNS resolve
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);
    struct addrinfo* res = nullptr;
    int gai = getaddrinfo(host, port_str, &hints, &res);
    if (gai != 0 || res == nullptr) {
        ESP_LOGE(TAG, "DNS resolve failed for %s: %d", host, gai);
        if (res) freeaddrinfo(res);
        return false;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        ESP_LOGE(TAG, "socket() failed: errno=%d (%s)", errno, strerror(errno));
        freeaddrinfo(res);
        return false;
    }

    // Apply connect timeout via SO_SNDTIMEO/RCVTIMEO. lwIP honors SO_SNDTIMEO
    // for connect().
    set_socket_timeout_ms(fd, NTRIP_CONNECT_TIMEOUT_SEC * 1000);

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG, "connect() to %s:%d failed: errno=%d (%s)",
                 host, port, errno, strerror(errno));
        close(fd);
        freeaddrinfo(res);
        return false;
    }
    freeaddrinfo(res);

    // Disable Nagle so small GGA writes go out immediately.
    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

    // Enable keepalive so a silent caster eventually closes our side. lwIP's
    // built-in defaults are idle=7200 s / intvl=75 s / cnt=9 — useless when a
    // 4G/MiFi handover silently kills the flow. Override per-socket so dead
    // sessions are detected in ~60 s (30 + 3 × 10) rather than ~2 h.
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));
    int keepidle = 30, keepintvl = 10, keepcnt = 3;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE,  &keepidle,  sizeof(keepidle));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT,   &keepcnt,   sizeof(keepcnt));

    // Send the GET request (loop in case of short writes)
    int sent_total = 0;
    while (sent_total < req_len) {
        int n = send(fd, request + sent_total, req_len - sent_total, 0);
        if (n <= 0) {
            ESP_LOGE(TAG, "send(request) failed: n=%d errno=%d", n, errno);
            close(fd);
            return false;
        }
        sent_total += n;
    }

    // Read the status line. NTRIP responses are either:
    //   "ICY 200 OK\r\n"                  (NTRIP/1.0)
    //   "HTTP/1.x 200 ..." + CRLF headers (NTRIP/2.0 or generic HTTP)
    //   "SOURCETABLE 200 OK\r\n"          (sourcetable request)
    char status_line[128];
    int status_len = read_line(fd, status_line, sizeof(status_line));
    if (status_len < 0) {
        ESP_LOGE(TAG, "read status line failed (errno=%d)", errno);
        close(fd);
        return false;
    }
    ESP_LOGI(TAG, "NTRIP status: %s", status_line);

    bool is_http = false;
    if (strncmp(status_line, "HTTP/1.", 7) == 0) {
        // Parse "HTTP/1.x NNN ..."
        int status_code = 0;
        if (sscanf(status_line, "HTTP/1.%*d %d", &status_code) != 1 ||
            status_code != 200) {
            ESP_LOGE(TAG, "NTRIP HTTP non-200: %s", status_line);
            close(fd);
            return false;
        }
        is_http = true;
    } else if (strncmp(status_line, "ICY 200 OK", 10) == 0) {
        is_http = false;  // NTRIP/1.0 — no headers follow, RTCM starts immediately
    } else if (strncmp(status_line, "SOURCETABLE 200 OK", 18) == 0) {
        is_http = false;  // sourcetable response
    } else {
        ESP_LOGE(TAG, "NTRIP unrecognized status: %s", status_line);
        close(fd);
        return false;
    }

    // For HTTP responses, consume headers until the blank line.
    if (is_http) {
        char header[256];
        while (true) {
            int hlen = read_line(fd, header, sizeof(header));
            if (hlen < 0) {
                ESP_LOGE(TAG, "read header failed");
                close(fd);
                return false;
            }
            if (hlen == 0) {
                break;  // blank line: end of headers
            }
            ESP_LOGD(TAG, "hdr: %s", header);
        }
    }

    // Switch to a short read timeout for the streaming phase so the task loop
    // can interleave GGA sends and RTCM reads without long stalls.
    set_socket_timeout_ms(fd, NTRIP_STREAM_READ_TIMEOUT_MS);

    sock_fd = fd;
    connected_flag = true;
    buffer_pos = 0;
    ESP_LOGI(TAG, "Successfully connected to NTRIP stream (fd=%d, %s)",
             fd, is_http ? "NTRIP/2.0" : "NTRIP/1.0");
    return true;
}

bool NTRIPClient::reqRaw(const char* host, int &port, const char* mntpnt) {
    return reqRaw(host, port, mntpnt, "", "");
}

bool NTRIPClient::reqSrcTblNoAuth(const char* host, int &port) {
    // Sourcetable request: GET / (empty mountpoint)
    return reqRaw(host, port, "", "", "");
}

bool NTRIPClient::reqSrcTbl(const char* host, int &port, const char* user, const char* psw) {
    return reqRaw(host, port, "", user, psw);
}

// ---- Streaming I/O ---------------------------------------------------------

void NTRIPClient::sendGGA(const char* gga) {
    if (sock_fd < 0 || !connected_flag) {
        ESP_LOGW(TAG, "sendGGA: not connected");
        return;
    }
    if (gga == nullptr) {
        return;
    }

    // Strip any trailing CR/LF from the input. The UART parser keeps the
    // trailing '\r'; without this strip the on-wire framing becomes
    // "...*XX\r\r\n", which strict casters reject.
    size_t in_len = strlen(gga);
    while (in_len > 0 && (gga[in_len - 1] == '\r' || gga[in_len - 1] == '\n')) {
        in_len--;
    }
    if (in_len == 0) {
        ESP_LOGW(TAG, "sendGGA: empty sentence");
        return;
    }

    char ggaString[256];
    int formatted = snprintf(ggaString, sizeof(ggaString), "%.*s\r\n",
                             (int)in_len, gga);
    if (formatted < 0 || formatted >= (int)sizeof(ggaString)) {
        ESP_LOGW(TAG, "sendGGA: truncated (formatted=%d)", formatted);
        formatted = (int)strlen(ggaString);
    }

    int sent_total = 0;
    while (sent_total < formatted) {
        int n = send(sock_fd, ggaString + sent_total, formatted - sent_total, 0);
        if (n <= 0) {
            ESP_LOGE(TAG, "sendGGA: send() failed n=%d errno=%d (%s) — marking connection lost",
                     n, errno, strerror(errno));
            connected_flag = false;
            statistics_gga_sent(false);
            return;
        }
        sent_total += n;
    }

    ESP_LOGI(TAG, "sendGGA: wrote %d bytes to caster: %.*s",
             sent_total, (int)in_len, gga);
    statistics_gga_sent(true);
}

int NTRIPClient::readData(uint8_t* data, size_t size) {
    if (sock_fd < 0 || !connected_flag) {
        return 0;
    }

    int n = recv(sock_fd, data, size, 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // No data within the SO_RCVTIMEO window — normal idle case.
            return 0;
        }
        ESP_LOGE(TAG, "readData: recv() failed errno=%d (%s)", errno, strerror(errno));
        statistics_ntrip_timeout();
        connected_flag = false;
        return -1;
    }
    if (n == 0) {
        // Peer closed connection cleanly (FIN).
        ESP_LOGW(TAG, "readData: peer closed connection");
        connected_flag = false;
        return -1;
    }
    return n;
}

int NTRIPClient::available() {
    if (sock_fd < 0 || !connected_flag) {
        return 0;
    }
    // Non-blocking readability check via select with zero timeout.
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sock_fd, &rfds);
    struct timeval tv = {0, 0};
    int rv = select(sock_fd + 1, &rfds, nullptr, nullptr, &tv);
    return (rv > 0 && FD_ISSET(sock_fd, &rfds)) ? 1 : 0;
}

int NTRIPClient::readLine(char* _buffer, int size) {
    if (sock_fd < 0 || !connected_flag || size <= 0) {
        return 0;
    }
    int total = 0;
    while (total < size - 1) {
        char c;
        int n = recv(sock_fd, &c, 1, 0);
        if (n <= 0) break;
        _buffer[total++] = c;
        if (c == '\n') break;
    }
    _buffer[total] = '\0';
    return total;
}

bool NTRIPClient::isConnected() {
    return connected_flag && sock_fd >= 0;
}

void NTRIPClient::disconnect() {
    if (sock_fd >= 0) {
        shutdown(sock_fd, SHUT_RDWR);
        close(sock_fd);
        sock_fd = -1;
    }
    connected_flag = false;
}
