# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/), and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Display a popup message in the browser when the connection to the ESP server is lost (periodic polling, auto-hide on reconnect)
- The UI password can now be configured directly from the web UI.
- Show a warning in the web UI when the UI password is still set to the factory default. The default password is now retrieved from the configuration manager for a single source of truth.
- Web UI now requires login with the UI password; session token is used for all API requests.
- All sensitive backend API endpoints now require authentication via session token (Authorization: Bearer ... header).
- Logout button added to the bottom of the main UI for easy session termination.
- Doxygen-compatible documentation for all major headers: gnssReceiverTask.h, dataOutputTask.h, configurationManagerTask.h, NMEAParser.h, statisticsTask.h.

### Changed
- Telemetry output now includes GNSS fix quality indicator before CRC-16
- Improved documentation for config_factory_reset to clarify NVS erase and restart requirements.
- Refactored statisticsTask.h to document all fields and structures for Doxygen.
- Updated frontend JavaScript to decouple connection lost popup from login/logout logic.

### Fixed
- Build error: missing declaration for led_indicator_task_init
- ButtonBootTask: Added and refactored boot button task for Lolin S3, including long-press detection (5s/10s) and FreeRTOS task structure.
- Centralized RGB LED control in ledIndicatorTask; RGB LED is now exclusively managed via a FreeRTOS queue.
- API: led_set_rgb() for inter-task RGB LED commands; button task now sends color commands to indicator task.
- RGB LED is initialized to off at startup.
- Doxygen documentation updated for ledIndicatorTask.h.
- Build and upload confirmed successful after all changes.
- Documentation and changelog kept up to date with new features and refactors.

### Removed
- Button task logic now uses led_set_rgb() and is consistent with other tasks.
- UI status indicators now update periodically via setInterval in frontend JavaScript.
- Refactored and cleaned up button and LED indicator task code; removed calculate_system_status_color and related logic.
- Removed calculate_system_status_color and all related status-based RGB LED logic from ledIndicatorTask.


## [2026-01-13]

### Added
- Doxygen-compatible documentation for all major headers: gnssReceiverTask.h, dataOutputTask.h, configurationManagerTask.h, NMEAParser.h, statisticsTask.h.
- Button long-press (>10s) now triggers config_factory_reset() and restarts ESP.
- Connection lost popup in web UI is now shown independently of login status; server connectivity is checked regardless of authentication.

### Changed
- Improved documentation for config_factory_reset to clarify NVS erase and restart requirements.
- Refactored statisticsTask.h to document all fields and structures for Doxygen.
- Updated frontend JavaScript to decouple connection lost popup from login/logout logic.

### Fixed
- Build and upload confirmed successful after all changes.
- Documentation and changelog kept up to date with new features and refactors.



## [Version x.y.z] - yyyy-mm-dd

### Added
- *Nothing.*

### Changed
- *Nothing.*

### Fixed
- *Nothing.*

### Removed
- *Nothing.*
