# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/), and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Display a popup message in the browser when the connection to the ESP server is lost (periodic polling, auto-hide on reconnect)
- The UI password can now be configured directly from the web UI.
- Show a warning in the web UI when the UI password is still set to the factory default. The default password is now retrieved from the configuration manager for a single source of truth.

### Changed
- Telemetry output now includes GNSS fix quality indicator before CRC-16

### Fixed
- Build error: missing declaration for led_indicator_task_init

### Removed
- *Nothing.*

## [Version x.y.z] - yyyy-mm-dd

### Added
- *Nothing.*

### Changed
- *Nothing.*

### Fixed
- *Nothing.*

### Removed
- *Nothing.*
