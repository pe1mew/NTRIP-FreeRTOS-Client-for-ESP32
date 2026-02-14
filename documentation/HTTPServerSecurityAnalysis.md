# HTTP Server Security Analysis

## Overview
This document analyzes the security implementation of the HTTP server in the espressiveFreeRTOS project, specifically examining authentication mechanisms and API access controls.

## Current Implementation

### Authentication Method
The HTTP server uses **session-based authentication** with Bearer tokens:

- **Token Storage**: Hardcoded static token in [httpServer.cpp:18](../src/httpServer.cpp#L18)
  ```c
  static const char* SESSION_TOKEN = "esp_session_token_123";
  ```

- **Authentication Check**: Implemented in [httpServer.cpp:518-527](../src/httpServer.cpp#L518-L527)
  - Extracts `Authorization` header
  - Validates `Bearer` prefix
  - Compares token against hardcoded value

### Endpoint Access Control

#### Public Endpoints (No Authentication Required)
1. **`GET /`** - Main web interface ([httpServer.cpp:694](../src/httpServer.cpp#L694))
2. **`POST /api/login`** - Authentication endpoint ([httpServer.cpp:26-58](../src/httpServer.cpp#L26-L58))
   - Validates UI password against NVS configuration
   - Returns session token on success

#### Protected Endpoints (Authentication Required)
All other API endpoints require valid Bearer token:

1. **`GET /api/config`** - Retrieve configuration ([httpServer.cpp:532-596](../src/httpServer.cpp#L532-L596))
2. **`POST /api/config`** - Update configuration ([httpServer.cpp:598-684](../src/httpServer.cpp#L598-L684))
3. **`GET /api/status`** - Device status ([httpServer.cpp:60-191](../src/httpServer.cpp#L60-L191))
4. **`POST /api/restart`** - Restart device ([httpServer.cpp:270-288](../src/httpServer.cpp#L270-L288))
5. **`POST /api/factory_reset`** - Factory reset ([httpServer.cpp:290-308](../src/httpServer.cpp#L290-L308))
6. **`POST /api/ntrip/toggle`** - Runtime NTRIP toggle ([httpServer.cpp:193-221](../src/httpServer.cpp#L193-L221))
7. **`POST /api/mqtt/toggle`** - Runtime MQTT toggle ([httpServer.cpp:223-251](../src/httpServer.cpp#L223-L251))
8. **`POST /api/wifi-scan`** - WiFi network scan ([httpServer.cpp:253-268](../src/httpServer.cpp#L253-L268))

## External API Access

### Is External Access Possible?
**YES** - The API can be accessed externally (outside the web interface) by any HTTP client that provides valid authentication.

### Access Method
1. **Obtain Token**: First authenticate via `/api/login` with valid UI password
   ```bash
   curl -X POST http://192.168.4.1/api/login \
     -H "Content-Type: application/json" \
     -d '{"password":"your_ui_password"}'
   ```
   Response: `{"status":"ok","token":"esp_session_token_123"}`

2. **Use Token**: Access protected endpoints with Bearer token
   ```bash
   curl http://192.168.4.1/api/status \
     -H "Authorization: Bearer esp_session_token_123"
   ```

### Authenticated Access Examples

#### Get Device Status
```bash
curl http://192.168.4.1/api/status \
  -H "Authorization: Bearer esp_session_token_123"
```

#### Get Configuration
```bash
curl http://192.168.4.1/api/config \
  -H "Authorization: Bearer esp_session_token_123"
```

#### Update Configuration
```bash
curl -X POST http://192.168.4.1/api/config \
  -H "Authorization: Bearer esp_session_token_123" \
  -H "Content-Type: application/json" \
  -d '{
    "wifi": {
      "ssid": "MyNetwork",
      "password": "MyPassword",
      "ap_password": "APPassword"
    },
    "ntrip": {
      "host": "rtk.example.com",
      "port": 2101,
      "mountpoint": "MOUNT1",
      "user": "user",
      "password": "pass",
      "gga_interval_sec": 120,
      "reconnect_delay_sec": 5,
      "enabled": true
    }
  }'
```

#### Toggle NTRIP Runtime
```bash
curl -X POST http://192.168.4.1/api/ntrip/toggle \
  -H "Authorization: Bearer esp_session_token_123" \
  -H "Content-Type: application/json" \
  -d '{"enabled": false}'
```

#### Restart Device
```bash
curl -X POST http://192.168.4.1/api/restart \
  -H "Authorization: Bearer esp_session_token_123"
```

#### Factory Reset
```bash
curl -X POST http://192.168.4.1/api/factory_reset \
  -H "Authorization: Bearer esp_session_token_123"
```

## Security Vulnerabilities

### 1. Hardcoded Static Token
**Severity**: HIGH

- Token is compiled into firmware: `esp_session_token_123`
- Same token used for all devices running this firmware
- Token cannot be changed without recompiling
- Anyone with firmware access knows the token

**Impact**: If the token is discovered, all devices are compromised.

### 2. No Token Expiration
**Severity**: MEDIUM

- Tokens never expire
- Once obtained, valid indefinitely
- No session timeout mechanism

**Impact**: Stolen tokens remain valid forever.

### 3. No Rate Limiting
**Severity**: MEDIUM

- Login endpoint has no rate limiting
- Allows unlimited password attempts
- Vulnerable to brute force attacks

**Impact**: UI password can be brute-forced.

### 4. No Token Revocation
**Severity**: MEDIUM

- No mechanism to invalidate tokens
- Cannot force logout
- No way to revoke compromised sessions

**Impact**: Cannot respond to security incidents.

### 5. Single Static Token for All Sessions
**Severity**: HIGH

- Same token shared across all sessions
- No per-session or per-client tokens
- Cannot distinguish between different clients

**Impact**: Cannot track or control individual sessions.

## Recommendations

### Priority 1: Dynamic Token Generation
Replace hardcoded token with randomly generated tokens:
- Generate unique token per login session
- Use cryptographically secure random number generator
- Store active tokens in memory with session metadata

### Priority 2: Token Expiration
Implement token lifecycle management:
- Add expiration timestamp to tokens
- Default timeout: 30-60 minutes
- Refresh mechanism for active sessions
- Automatic cleanup of expired tokens

### Priority 3: Rate Limiting
Add login attempt protection:
- Limit login attempts per IP address
- Exponential backoff after failures
- Temporary lockout after excessive attempts

### Priority 4: Token Revocation
Implement session management:
- Logout endpoint to invalidate tokens
- Track active sessions
- Ability to revoke specific tokens
- Force logout all sessions option

### Priority 5: Enhanced Security Headers
Add HTTP security headers:
- `X-Content-Type-Options: nosniff`
- `X-Frame-Options: DENY`
- `Content-Security-Policy`

### Priority 6: HTTPS Support
Consider adding TLS/SSL:
- Encrypt all communication
- Prevent token interception
- Protect credentials in transit

## Current Security Posture

**Strengths**:
- Password-based authentication protects API access
- Bearer token mechanism properly implemented
- Clear separation of public vs protected endpoints
- All configuration changes require authentication

**Weaknesses**:
- Static hardcoded token compromises all security
- No defense against token theft or reuse
- Vulnerable to brute force attacks
- Cannot respond to security incidents

## Conclusion

The HTTP server implements **basic authentication**, but with significant security limitations due to the hardcoded static token. External API access is **fully supported** and **intentionally designed** - any HTTP client can authenticate and access the API.

For production use or security-sensitive deployments, implementing dynamic token generation and expiration should be considered critical priorities.

---

**Analysis Date**: 2026-01-21
**Firmware Version**: Based on current implementation
**Analyzed Files**: [src/httpServer.cpp](../src/httpServer.cpp), [src/configurationManagerTask.h](../src/configurationManagerTask.h)
