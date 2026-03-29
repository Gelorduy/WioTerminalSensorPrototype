# Changelog

## 2026-03-28

### Added

- Signed ACK verification support via `X-Ack-Signature` (HMAC-SHA256 over payload)
- Runtime ACK verification toggle in Config window (default ON)
- BLE rename unlock workflow with configurable unlock windows (`30s`, `60s`, `120s`)
- `BLEU` bottom-right indicator while BLE rename is unlocked
- User manual at `otherfiles/documents/USER_MANUAL.md`

### Changed

- BLE rename unlock moved to top button `C` to reduce control lag and simplify operation
- BLE rename writes now require local unlock window and sanitized input
- Config window expanded with security and BLE status details

### Notes

- Signed ACK verification requires `API_ACK_HMAC_KEY` and server header `X-Ack-Signature`.

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
