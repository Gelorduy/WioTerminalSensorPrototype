# Procomsa Wio Monitor - User Manual

## 1. Purpose

This device measures temperature/humidity, logs readings locally, and uploads data securely to a remote API when connectivity is available.

## 2. Hardware

- Seeed Wio Terminal
- SHT35 sensor
- SD card installed
- Wi-Fi network available

## 3. First-Time Setup

1. Configure secrets in `include/secrets_local.h`.
2. Required values:
   - `WIFI_HOSTNAME`
   - `WIFI_SSID_PRIMARY`, `WIFI_PASSWORD_PRIMARY`
   - `WIFI_SSID_ALTERNATE`, `WIFI_PASSWORD_ALTERNATE`
   - `WIFI_SSID_MOBILE`, `WIFI_PASSWORD_MOBILE`
   - `API_SERVER_URL`
   - `API_BEARER_TOKEN`
3. Optional (recommended for secure ACK validation):
   - `API_ACK_HMAC_KEY`
4. Build and upload firmware:

```bash
~/.platformio/penv/bin/pio run
~/.platformio/penv/bin/pio run -t upload
```

## 4. Startup Behavior

- Startup monitor displays sensor, SD, BLE, Wi-Fi, and NTP status.
- If Wi-Fi cannot connect immediately, diagnostics are shown and retry continues.
- Device enters main screen after startup sequence.

## 5. Controls

- `A`: wake screen for timed view
- `B`: toggle always-on display mode (4 second debounce)
- `C`: unlock BLE rename writes for configured window
- `Joystick RIGHT`: open menu (from main)
- `Joystick LEFT`: go back
- `Joystick UP/DOWN`: navigate or scroll
- `Joystick PRESS`: select/open

## 6. Screen Navigation

1. Main Screen
2. Menu
3. Log Menu
   - Temperature Log
   - Events Log
4. Log Viewer (paged view)
5. Config Window

## 7. Config Window Functions

- Shows firmware version
- Shows ACK verification state (`ON`/`OFF`)
- Shows ACK key state (`key configured` / `key missing`)
- Shows BLE rename state (`LOCKED` / `UNLOCKED`) and remaining unlock time
- Shows Wi-Fi SSID/IP

Controls in Config:

- `PRESS`: toggle ACK verification
- `UP/DOWN`: cycle BLE unlock window duration (`30s`, `60s`, `120s`)

## 8. BLE Rename Workflow

To rename device location safely:

1. Go to Config and choose desired unlock window (`30s`, `60s`, or `120s`).
2. Press `C` to unlock BLE rename.
3. Confirm `BLE Rename: UNLOCKED` in Config.
4. Confirm `BLEU` badge appears bottom-right on screens.
5. Send new place/name via Bluetooth app.

When unlocked time expires:

- BLE rename writes are rejected again.
- `BLEU` badge disappears.

## 9. Logging

Files stored on SD card:

- `readings<serial>.log`: periodic measurements
- `unsent<serial>.log`: queued payloads awaiting confirmed upload
- `events<serial>.log`: startup/network/security diagnostics

Log rotation:

- Files rotate around 128 KB and create `.bak` backups.

## 10. Upload and Retry Model

- Every reading is queued first on SD.
- Background worker retries uploads.
- Payload is removed only when upload is confirmed.
- Non-confirmed payloads remain queued.

## 11. Secure ACK Validation (Optional but Recommended)

If `API_ACK_HMAC_KEY` is configured and ACK validation is ON:

- Device expects `X-Ack-Signature` response header on HTTP 200.
- Signature must be lowercase hex HMAC-SHA256 of the exact POST body.
- If signature is missing/invalid, payload stays queued.

## 12. Wi-Fi Recovery

- Reconnection is non-blocking.
- Device periodically rescans for target SSIDs.
- Sequential retries are attempted across configured SSIDs.
- Exponential backoff protects stability.

## 13. Troubleshooting

### Buttons feel slow

- Keep screen on Main for lowest background overhead.
- Verify Wi-Fi signal quality and SD card health.

### No uploads

- Check Wi-Fi status in Config.
- Inspect `events<serial>.log` for `https` and `queue` messages.
- Verify `API_SERVER_URL` and token.
- If ACK validation ON, verify server sends correct `X-Ack-Signature`.

### Cannot rename via Bluetooth

- Press `C` first.
- Confirm `UNLOCKED` state and `BLEU` badge.
- Retry within unlock window time.

### Time is wrong

- Check Wi-Fi connectivity.
- Verify NTP sync messages in events log.

## 14. Operational Recommendations

- Keep ACK validation enabled in production.
- Use strong, private values for `API_BEARER_TOKEN` and `API_ACK_HMAC_KEY`.
- Review event logs periodically.
- Rotate credentials if compromise is suspected.
