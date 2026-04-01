# GGGWioMonitor Refactor Roadmap

## Goal
Improve maintainability and reliability of the WIO Terminal firmware with low-risk, phased changes that keep current behavior stable.

## Current Constraints
- Board/platform stack: `seeed_wio_terminal` + `atmelsam` + Arduino.
- Legacy include behavior existed for SHT35 (`.pio/libdeps` direct path).
- Credentials and API token are currently hardcoded in `src/main.cpp`.

## Include Compatibility Plan (SHT35)
A compatibility include strategy is now in `src/main.cpp`:

```cpp
#if __has_include(<Seeed_SHT35.h>)
#include <Seeed_SHT35.h>
#else
#include "../.pio/libdeps/seeed_wio_terminal/Grove - I2C High Accuracy Temp_Humi Sensor SHT35/Seeed_SHT35.h"
#endif
```

Why this helps:
- Uses standard PlatformIO include resolution when available.
- Preserves your known working fallback for environments where resolver behavior is inconsistent.

Validation to run on your machine:
1. `pio run`
2. If successful, keep as-is.
3. If fallback path is used unexpectedly, review `platformio.ini` library versions and cache state (`pio pkg update`, clean build).

## Phase 1: Safety and Configuration
1. Move secrets to `include/secrets_local.h` (git-ignored).
2. Add `include/secrets_template.h` for onboarding.
3. Replace literals in `main.cpp` with named config constants.

Acceptance:
- Firmware builds and runs with local secrets file.
- No real credentials remain in tracked files.

## Phase 2: Module Split (No Behavior Change)
1. Extract to files:
   - `src/network.cpp/.h`
   - `src/sensors.cpp/.h`
   - `src/display.cpp/.h`
   - `src/ble.cpp/.h`
   - `src/storage.cpp/.h`
2. Keep `main.cpp` as orchestration only.

Acceptance:
- Binary still builds.
- Same serial logs and periodic behavior as before.

## Phase 3: Reliability Improvements
1. Implement resend queue for unsent logs at boot and periodic intervals.
2. Add Wi-Fi reconnect backoff with max retry interval.
3. Add log rotation limits to protect SD lifespan/storage.

Acceptance:
- Network outage no longer loses data.
- Device recovers automatically when network returns.

## Phase 4: Performance and Memory Hygiene
1. Remove dynamic allocation in `sendToScreen()` for date format string.
2. Minimize repeated string copies in JSON and BLE update paths.
3. Reduce blocking `delay()` where practical.

Acceptance:
- Stable long-run execution without progressive memory issues.
- UI refresh and sensor loop remain responsive.

## Phase 5: Verification and Release Process
1. Add a hardware regression checklist for each release.
2. Add versioned release notes in `otherfiles/documents/`.
3. Add a small smoke test routine (boot, sensor read, BLE adv, SD write, HTTPS attempt).

Acceptance:
- Repeatable validation before deployment.
- Faster debugging when field issues appear.

## Suggested `platformio.ini` Hardening
Consider these optional improvements:

```ini
[env:seeed_wio_terminal]
platform = atmelsam
board = seeed_wio_terminal
framework = arduino
lib_ldf_mode = deep+
lib_compat_mode = strict
```

Notes:
- `lib_ldf_mode = deep+` can improve dependency discovery in complex projects.
- Test carefully after enabling; behavior may differ across PlatformIO versions.
