# ProcomsaWioMonitor - Architecture and Dependencies

## 1. Project Snapshot
- Project type: PlatformIO + Arduino framework + C++
- Board target in `platformio.ini`: `seeed_wio_terminal`
- MCU family: ATSAMD51 (not ESP32 main MCU)
- Connectivity coprocessor on WIO Terminal: RTL8720 (used through `rpcWiFi` and `rpcBLE`)

Note:
- The current request mentions ESP32, but this repository is configured for Seeed WIO Terminal (SAMD51 + RTL8720 subsystem).

## 2. Current Structure
- `platformio.ini`: build target and library dependencies
- `src/main.cpp`: main application logic (sensors, networking, BLE, display, SD logging)
- `src/millisDelay.*`: reusable non-blocking timing helper
- `src/RawImage.h`: image loading/drawing helper for SD + TFT
- `src/Free_Fonts.h`: font aliases for TFT rendering
- `otherfiles/documents/`: project documentation

## 3. Runtime Architecture

### 3.1 Startup (`setup()`)
1. Initializes serial and IO pins (light sensor, buzzer, keys).
2. Initializes TFT display and sprite buffer.
3. Initializes SD card and checks log files.
4. Initializes SHT40 and SHT35 sensors over I2C.
5. Scans Wi-Fi and connects to preferred SSID.
6. Syncs RTC from NTP (`mx.pool.ntp.org`) if Wi-Fi is available.
7. Configures TLS certificate for HTTPS client.
8. Initializes BLE services/characteristics:
   - Read service: temperature + humidity (notify/read)
   - Write service: location/place (read/write)
9. Starts BLE advertising.

### 3.2 Main Loop (`loop()`)
- Key handling:
  - `WIO_KEY_A`: turn screen on for a timed window.
  - `WIO_KEY_B`: toggle persistent screen mode.
- Periodic task every 60 seconds:
  - Read sensors.
  - Capture light value.
  - Optionally send JSON payload by HTTPS POST.
  - Save readings to SD logs (`readings*.log`, `unsent*.log`).
  - Update BLE values and diagnostics.
- Screen update:
  - Refreshes temperature/humidity/time/light view.
  - Uses backlight timeout for power saving.

## 4. Functional Blocks
- Sensor acquisition: SHT40 (Sensirion) + SHT35 (Grove)
- Timekeeping: NTP over UDP + RTC_SAMD51
- Networking: `rpcWiFi` + TLS (`WiFiClientSecure`) + `HTTPClient`
- BLE telemetry: `rpcBLE` GATT services/characteristics
- UI: `TFT_eSPI` + custom fonts + optional raw image rendering
- Storage: SD card logs for sent/unsent buffering

## 5. Library and Dependency Review (WIO Terminal)

### 5.1 Declared in `platformio.ini`
- `seeed-studio/Seeed Arduino rpcWiFi`
- `seeed-studio/Seeed Arduino rpcUnified`
- `sensirion/Sensirion I2C SHT4x`
- `seeed-studio/Seeed Arduino FS`
- `seeed-studio/Seeed Arduino SFUD`
- `seeed-studio/Seeed_Arduino_mbedtls`
- `bblanchon/ArduinoJson`
- `seeed-studio/Seeed Arduino RTC`
- `seeed-studio/Grove - I2C High Accuracy Temp_Humi Sensor SHT35`
- `seeed-studio/Seeed Arduino rpcBLE`

### 5.2 Used in code and expected from board/framework ecosystem
- `TFT_eSPI.h`
- `HTTPClient.h`
- `rpcWiFiClientSecure.h`
- `SD/Seeed_SD.h`
- `Wire.h`, `SPI.h`

These are typically available through the Seeed WIO Terminal Arduino core stack, but if a clean build fails, pinning explicit display/SD/network libs in `lib_deps` is recommended.

## 6. Dependency Notes for Seeed Studio WIO Terminal
- Keep board as `seeed_wio_terminal` with `platform = atmelsam` and `framework = arduino`.
- Use Seeed `rpc*` libraries (`rpcWiFi`, `rpcBLE`) for RTL8720 communications.
- Keep `Seeed Arduino RTC` for SAMD51 RTC handling.
- Keep `Seeed Arduino FS`, `Seeed_SD`, and `SFUD` for storage/flash paths.
- Ensure sensor libraries match physical hardware actually connected (SHT40 onboard/external, SHT35 Grove module).

## 7. Important Findings and Risks
- Hardcoded secrets in source:
  - Wi-Fi SSIDs/passwords
  - Bearer token
  - Server URL
- Include path anti-pattern:
  - `#include "../.pio/libdeps/.../Seeed_SHT35.h"`
  - This path is machine/build-folder specific and fragile.
  - Prefer `#include <Seeed_SHT35.h>` and let PlatformIO resolve from `lib_deps`.
- Minor memory leak risk in `sendToScreen()`:
  - `new char[12]` allocated each refresh and never freed.

## 8. Suggested Next Improvements
1. Move credentials to build flags or a local ignored config header.
2. Replace the `.pio` absolute include with library include style.
3. Add a retry queue for unsent logs and resend when Wi-Fi returns.
4. Add a lightweight hardware integration test checklist.
