# Changelog

## 2026-03-28

### Added

- Joystick-driven window navigation (`Main`, `Menu`, `Log Menu`, `Log Viewer`, `Config`)
- Split log browsing for temperature readings and events, with page scrolling
- SD-backed events log stream for startup, network, queue, NTP, and upload diagnostics
- Queue-first upload pipeline with `HTTP 200` confirmation semantics
- Startup Wi-Fi monitor with detailed target and status reporting

### Changed

- Display rendering moved to sprite-based window drawing with push-once frames
- Added sprite allocation fallback from 16-bit to 8-bit color depth for safer RAM usage
- Main display refresh now throttled to reduce flicker while always-on
- Wi-Fi reconnect converted to non-blocking state machine with periodic rescans
- Reconnect logic now handles connection-lost/connect-failed states with clean attempt reset
- NTP handling now uses fallback servers and updates RTC on successful sync
- Queue retry cadence adjusted to avoid impacting button responsiveness

### Notes

- Existing third-party `BYTE_ORDER` warning from dependency headers remains non-blocking.
