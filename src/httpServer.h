#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "esp_err.h"
#include "esp_http_server.h"

/**
 * @brief Initialize and start the HTTP server
 * 
 * Starts the HTTP server on port 80 with handlers for:
 * - Static content (/, /style.css, /app.js, /favicon.ico)
 * - REST API (/api/config, /api/status, /api/restart, /api/factory_reset)
 * 
 * The server listens on the Access Point interface (192.168.4.1).
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t http_server_start(void);

/**
 * @brief Stop the HTTP server
 * 
 * Shuts down the HTTP server and releases resources.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t http_server_stop(void);

/**
 * @brief Check if HTTP server is running
 * 
 * @return true if server is running, false otherwise
 */
bool http_server_is_running(void);

#endif // HTTP_SERVER_H
