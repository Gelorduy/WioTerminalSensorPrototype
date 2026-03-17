# WIO Terminal Development Guide (PlatformIO)

## 1. Daily Workflow

### 1.1 Build and Upload
Use from the project root:

```bash
pio run
pio run -t upload
pio device monitor -b 9600
```

If `pio` is not found in a regular shell but works in the VS Code `PlatformIO CLI` terminal, use one of these options:
- Run commands from the `PlatformIO CLI` terminal.
- Use the direct executable path:

```bash
~/.platformio/penv/bin/pio run
~/.platformio/penv/bin/pio run -t upload
~/.platformio/penv/bin/pio device monitor -b 9600
```

### 1.2 Clean Rebuild
```bash
pio run -t clean
pio run
```

## 2. Configuration Strategy
- Keep `platformio.ini` as the single source of truth.
- Prefer fixed versions for core dependencies to avoid regressions.
- When adding a new sensor or protocol, add one library at a time and verify build.

Example pattern:
```ini
lib_deps =
  vendor/library-name@^x.y.z
```

## 3. Secure Configuration
Do not keep production credentials in `src/main.cpp`.

Recommended pattern:
1. Create `include/secrets_local.h` (git-ignored).
2. Define constants there (`WIFI_SSID`, `WIFI_PASSWORD`, `API_TOKEN`, `API_URL`).
3. Include it from `main.cpp`.
4. Add `include/secrets_template.h` with placeholder values for onboarding.

Current implementation:
- `main.cpp` loads `secrets_local.h` when present and falls back to `secrets_template.h`.
- `include/secrets_local.h` is in `.gitignore`.

## 4. Reliability Checklist
Before each firmware release:
1. Verify cold boot with and without SD card inserted.
2. Verify Wi-Fi unavailable path logs unsent readings.
3. Verify successful HTTPS POST when Wi-Fi returns.
4. Verify BLE advertisement and characteristic read/write.
5. Verify screen timeout/backlight behavior and button overrides.
6. Verify RTC sync after power cycle.

## 5. Logging and Offline Buffering
- Keep two logical log streams:
  - `readings*.log`: all attempts/measurements
  - `unsent*.log`: only failed uploads
- Add a resend routine at startup and periodic intervals.
- Trim or rotate log files to avoid SD exhaustion.

## 6. Coding Guidelines for This Project
- Prefer non-blocking timing (`millis`) over `delay` in runtime loop paths.
- Keep `setup()` for initialization only; move repeated logic to dedicated functions.
- Keep sensor, network, BLE, and UI concerns separated in functions/files.
- Use `DynamicJsonDocument` capacities conservatively and document expected payload sizes.

## 7. WIO Terminal Specific Tips
- WIO Terminal main MCU is SAMD51; Wi-Fi/BLE are via RTL8720 RPC bridge.
- If `rpcWiFi` behaves unexpectedly, confirm firmware/library compatibility in Seeed stack.
- Display stack (`TFT_eSPI`) may rely on Seeed core packaging; pin explicit versions if build starts failing after updates.

## 8. Suggested Backlog
1. Refactor into modules: `network.cpp`, `sensors.cpp`, `display.cpp`, `ble.cpp`, `storage.cpp`.
2. Add compile-time feature flags (`ENABLE_BLE`, `ENABLE_WIFI_POST`, `ENABLE_SD_LOG`).
3. Add a lightweight diagnostic screen with system state (Wi-Fi RSSI, free heap, last post status).
4. Add host-side JSON payload schema validation test.
