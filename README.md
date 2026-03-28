# Procomsa Wio Monitor

Firmware for a Seeed Wio Terminal environmental monitor with:

- SHT35 temperature/humidity capture
- Wi-Fi + HTTPS upload with disk-first retry queue
- RTC timekeeping synchronized from NTP
- SD-backed readings and events logs
- Joystick-driven multi-window UI
- Buffered TFT rendering with safe sprite fallback

## Build And Upload

```bash
~/.platformio/penv/bin/pio run
~/.platformio/penv/bin/pio run -t upload
```

Target environment: `seeed_wio_terminal`.

## Runtime Controls

- `Top button A`: wake screen for timed display window
- `Top button B`: toggle always-on display mode (4s debounce)
- `Joystick RIGHT`: open menu from main screen
- `Joystick LEFT`: back to previous window
- `Joystick UP/DOWN`: move selection or scroll log pages
- `Joystick PRESS`: select/open

## Display And UI

Window flow:

- Main screen
- Menu
- Log menu (Temperature Log / Events Log)
- Log viewer (paged scrolling)
- Config screen (Wi-Fi status overview)

Rendering behavior:

- UI windows are rendered with a full-frame sprite and pushed once per refresh
- Sprite allocation attempts 16-bit first, then 8-bit fallback if memory is tight
- Main redraw cadence is throttled (`screenRefreshInterval`) to reduce flicker

## Logging

SD files use the sensor serial suffix:

- `readings<serial>.log`: sampled environment entries
- `unsent<serial>.log`: pending HTTPS payload queue
- `events<serial>.log`: startup/network/upload/display diagnostics

Large logs are rotated at ~128 KB with `.bak` rollover.

## Upload Reliability Model

Posting is queue-first:

1. Sample is appended to `unsent<serial>.log`
2. Background worker retries queued payloads
3. Entry is removed only on HTTP `200`
4. Any non-`200` or connection failure remains queued

Queue processing is intentionally limited while the user is interacting to preserve UI responsiveness.

## Wi-Fi Recovery Strategy

Connection management is non-blocking and loop-driven:

- Periodic scan while disconnected to refresh visible target SSIDs
- Sequential attempts across configured SSIDs
- Per-cycle backoff (up to capped max)
- Clean reconnect path after `WL_CONNECTION_LOST` / `WL_CONNECT_FAILED`
- Live status strings available for startup monitor and on-screen diagnostics

## Time Synchronization

NTP sync uses fallback servers and updates RTC when available.

- Sync at startup when Wi-Fi is available
- Periodic resync in runtime
- NTP outcomes are mirrored into events log for diagnostics

## Configuration

Create `include/secrets_local.h` (or edit existing) with your local values:

- `WIFI_SSID_PRIMARY`, `WIFI_PASSWORD_PRIMARY`
- `WIFI_SSID_ALTERNATE`, `WIFI_PASSWORD_ALTERNATE`
- `WIFI_SSID_MOBILE`, `WIFI_PASSWORD_MOBILE`
- `API_SERVER_URL`
- `API_BEARER_TOKEN`
- `WIFI_HOSTNAME`

`secrets_local.h` is preferred for local/private deployment credentials.
