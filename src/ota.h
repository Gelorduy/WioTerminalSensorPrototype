#ifndef OTA_H
#define OTA_H

#include <Arduino.h>

enum class OtaState {
    IDLE,          // No OTA activity
    CHECKING,      // HTTPS GET to version endpoint in progress
    AVAILABLE,     // Newer version found on server, will download next
    DOWNLOADING,   // Streaming firmware .bin to SD card
    STAGING,       // Copying .bin from SD to upper internal flash (staging area)
    READY,         // Staged; waiting for user to press APPLY
    APPLYING,      // User confirmed; will reboot to apply on next loop() tick
    UP_TO_DATE,    // Checked and current version is the latest
    FAILED         // An error occurred (see getStatusMessage)
};

// ── Must be called at the VERY TOP of setup(), before any hardware init ───────
// If the previous boot staged a firmware update, this will apply it by running
// the RAM trampoline (copies upper flash → lower flash) and resetting.
// Returns immediately if no update is pending.
void     otaInit();

// ── Runtime API ──────────────────────────────────────────────────────────────
// Kick off a version check on the next otaProcessStateMachine() call.
void     otaCheckForUpdate();

// Drive the state machine.  Call once per loop().
void     otaProcessStateMachine();

// Accessors
OtaState otaGetState();
String   otaGetAvailableVersion();
String   otaGetStatusMessage();
int      otaGetDownloadPercent();   // 0–100 during DOWNLOADING

// User-confirmed apply: transitions READY → APPLYING.
void     otaRequestApply();

#endif // OTA_H
