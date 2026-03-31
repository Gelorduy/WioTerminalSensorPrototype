#include "display.h"

#include <Arduino.h>
#include <rpcWiFi.h>
#include "SD/Seeed_SD.h"
#include "Free_Fonts.h"
#include "app_state.h"
#include "network.h"
#include "storage.h"

#define TFT_PROCOMSABLUE 0x2A51

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

// Smaller WiFi icon for bottom of screen
static void drawWiFiIconSmall(int cx, int cy, bool connected) {
    uint16_t color = connected ? TFT_GREEN : TFT_RED;
    uint16_t bg    = TFT_BLACK;
    // Dot
    spr.fillCircle(cx, cy, 1, color);
    // Small arc
    spr.drawCircle(cx, cy, 4, color);
    spr.fillRect(cx - 5, cy + 1, 10, 5, bg);
    // Medium arc
    spr.drawCircle(cx, cy, 8, color);
    spr.fillRect(cx - 9, cy + 1, 18, 8, bg);
}

static void drawBleUnlockBadge() {
    bool bleUnlocked = millis() < bleRenameUnlockUntilMs;
    if (!bleUnlocked) {
        return;
    }

    const int badgeW = 48;
    const int badgeH = 16;
    const int badgeX = 320 - badgeW - 4;
    const int badgeY = 240 - badgeH - 4;
    spr.fillRoundRect(badgeX, badgeY, badgeW, badgeH, 4, TFT_GREEN);
    spr.setTextColor(TFT_BLACK, TFT_GREEN);
    spr.setTextFont(1);
    spr.drawString("BLEU", badgeX + 6, badgeY + 4);
}

static String fitHeaderText(const String& text, size_t maxLen) {
    if (text.length() <= maxLen) {
        return text;
    }

    if (maxLen <= 3) {
        return text.substring(0, maxLen);
    }

    return text.substring(0, maxLen - 3) + "...";
}

static int startupLineY = 24;
static const int startupLineHeight = 16;
static const int startupLogTop = 24;
static const int startupLogBottom = 238;

static uint16_t startupStatusColor(const String& message) {
    String probe = message;
    probe.toLowerCase();

    if (probe.indexOf("fail") >= 0 ||
        probe.indexOf("error") >= 0 ||
        probe.indexOf("not found") >= 0 ||
        probe.indexOf("no ") == 0) {
        return TFT_RED;
    }

    if (probe.indexOf("ok") >= 0 ||
        probe.indexOf("ready") >= 0 ||
        probe.indexOf("connected") >= 0 ||
        probe.indexOf("active") >= 0 ||
        probe.indexOf("started") >= 0 ||
        probe.indexOf("initialized") >= 0 ||
        probe.indexOf("checked") >= 0 ||
        probe.indexOf("complete") >= 0) {
        return TFT_GREEN;
    }

    return TFT_WHITE;
}

void beginStartupStatus() {
    startupLineY = startupLogTop;
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, 320, 20, TFT_PROCOMSABLUE);
    tft.setTextColor(TFT_WHITE, TFT_PROCOMSABLUE);
    tft.setTextFont(2);
    tft.drawString("Startup monitor", 6, 4);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("Boot sequence:", 6, startupLogTop);
    startupLineY += startupLineHeight;
}

void logStartupStatus(const String& message) {
    appendEventLog("startup: " + message);
    if (startupLineY + startupLineHeight > startupLogBottom) {
        tft.fillRect(0, startupLogTop, 320, startupLogBottom - startupLogTop, TFT_BLACK);
        startupLineY = startupLogTop;
    }
    tft.setTextColor(startupStatusColor(message), TFT_BLACK);
    tft.setTextFont(2);
    tft.drawString(message, 6, startupLineY);
    startupLineY += startupLineHeight;
}

void endStartupStatus() {
    logStartupStatus("Init complete, loading data screen...");
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("READY", 6, startupLineY);
}

static void drawWindowHeader(const String& title, const String& subtitle) {
    spr.fillSprite(TFT_BLACK);
    spr.fillRect(0, 0, 320, 28, TFT_PROCOMSABLUE);
    spr.setTextColor(TFT_WHITE, TFT_PROCOMSABLUE);
    spr.setTextFont(2);
    spr.drawString(title, 8, 6);
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    spr.drawString(subtitle, 8, 34);
}

void sendMenuScreen(int selectedIndex) {
    static const char* options[3] = {
        "Show Log",
        "Main Screen",
        "Config Screen"
    };

    drawWindowHeader("Menu", "UP/DOWN to move, PRESS to select");

    int y = 72;
    for (int i = 0; i < 3; ++i) {
        bool selected = (i == selectedIndex);
        uint16_t bg = selected ? TFT_PROCOMSABLUE : TFT_BLACK;
        uint16_t fg = selected ? TFT_WHITE : TFT_LIGHTGREY;

        spr.fillRoundRect(14, y - 4, 292, 28, 6, bg);
        spr.setTextColor(fg, bg);
        spr.setTextFont(2);
        spr.drawString(String(selected ? "> " : "  ") + options[i], 22, y);
        y += 42;
    }

    spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
    spr.drawString("RIGHT from Main opens this menu", 8, 214);
    drawBleUnlockBadge();
    spr.pushSprite(0, 0);
}

void sendLogMenuScreen(int selectedIndex) {
    static const char* options[2] = {
        "Temperature Log",
        "Events Log"
    };

    drawWindowHeader("Log Menu", "UP/DOWN move, PRESS open, LEFT back");

    int y = 84;
    for (int i = 0; i < 2; ++i) {
        bool selected = (i == selectedIndex);
        uint16_t bg = selected ? TFT_PROCOMSABLUE : TFT_BLACK;
        uint16_t fg = selected ? TFT_WHITE : TFT_LIGHTGREY;

        spr.fillRoundRect(14, y - 4, 292, 30, 6, bg);
        spr.setTextColor(fg, bg);
        spr.setTextFont(2);
        spr.drawString(String(selected ? "> " : "  ") + options[i], 22, y);
        y += 44;
    }

    spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
    spr.drawString("Select which log to browse", 8, 214);
    drawBleUnlockBadge();
    spr.pushSprite(0, 0);
}

void sendLogViewerScreen(bool eventsLog, int scrollOffset) {
    const String logName = eventsLog ? ("events" + String(serialNumber) + ".log")
                                     : ("readings" + String(serialNumber) + ".log");
    const String subtitle = eventsLog ? "Events log (UP/DOWN scroll)" : "Temperature log (UP/DOWN scroll)";

    drawWindowHeader(eventsLog ? "Events Log" : "Temperature Log", subtitle);

    spr.setTextFont(2);
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawString("File: " + fitHeaderText(logName, 30), 8, 56);

    File logFile = SD.open(logName, FILE_READ);
    if (!logFile) {
        spr.setTextColor(TFT_RED, TFT_BLACK);
        spr.drawString("No log file found", 8, 92);
        spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
        spr.drawString("LEFT: back to log menu", 8, 214);
        spr.pushSprite(0, 0);
        return;
    }

    const int kVisibleLines = 10;
    String lines[kVisibleLines];
    int lineIndex = 0;
    int shown = 0;

    while (logFile.available()) {
        String line = logFile.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) {
            continue;
        }
        if (lineIndex >= scrollOffset && shown < kVisibleLines) {
            lines[shown] = line;
            shown++;
        }
        lineIndex++;
        if (shown >= kVisibleLines) {
            break;
        }
    }
    logFile.close();

    if (shown == 0) {
        spr.setTextColor(TFT_ORANGE, TFT_BLACK);
        spr.drawString("No entries at this position", 8, 92);
    } else {
        int y = 72;
        for (int i = 0; i < shown; ++i) {
            spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
            spr.drawString(fitHeaderText(lines[i], 43), 8, y);
            y += 15;
        }
    }

    spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
    spr.drawString("Offset: " + String(scrollOffset) + "  LEFT: menu", 8, 214);
    drawBleUnlockBadge();
    spr.pushSprite(0, 0);
}

static void logMainScreenSnapshot(const String& location,
                                  const String& screenTime,
                                  float temperature,
                                  float humidity,
                                  int lightPercent,
                                  const String& wifiName,
                                  const String& wifiIp,
                                  bool wifiConnected,
                                  const String& wifiFailureReason) {
    String snapshot = "main: loc=" + location +
                      " time=" + screenTime +
                      " temp=" + String(temperature, 2) + "C" +
                      " hum=" + String(humidity, 2) + "%" +
                      " light=" + String(lightPercent) + "%" +
                      " wifi=" + (wifiConnected ? wifiName : "offline") +
                      " ip=" + (wifiConnected ? wifiIp : "no ip");
    if (!wifiConnected && wifiFailureReason.length() > 0) {
        snapshot += " reason=" + wifiFailureReason;
    }
    appendEventLog(snapshot);
}

void sendConfigScreen() {
    drawWindowHeader("Config Window", "RIGHT ack, PRESS SD reset");
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.setTextFont(2);
    spr.drawString("Version:", 8, 66);
    spr.setTextColor(TFT_YELLOW, TFT_BLACK);
    spr.drawString(String(APP_VERSION), 8, 88);

    bool ackEnabled = isAckValidationEnabled();
    bool ackConfigured = isAckValidationConfigured();
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawString("Ack Verify:", 168, 66);
    spr.setTextColor(ackEnabled ? TFT_GREEN : TFT_RED, TFT_BLACK);
    spr.drawString(ackEnabled ? "ON" : "OFF", 168, 88);
    spr.setTextColor(ackConfigured ? TFT_LIGHTGREY : TFT_ORANGE, TFT_BLACK);
    spr.drawString(ackConfigured ? "key configured" : "key missing", 168, 108);

    bool bleRenameUnlocked = millis() < bleRenameUnlockUntilMs;
    unsigned long bleSecondsLeft = bleRenameUnlocked ? ((bleRenameUnlockUntilMs - millis()) / 1000UL) : 0;
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawString("BLE Rename:", 168, 126);
    spr.setTextColor(bleRenameUnlocked ? TFT_GREEN : TFT_RED, TFT_BLACK);
    spr.drawString(bleRenameUnlocked ? "UNLOCKED" : "LOCKED", 168, 146);
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    if (bleRenameUnlocked) {
        spr.drawString(String(bleSecondsLeft) + "s left", 168, 166);
    } else {
        spr.drawString("Press C unlock", 168, 166);
    }
    spr.drawString("Window: " + String(bleRenameUnlockWindowMs / 1000UL) + "s", 168, 184);

    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawString("Current WiFi:", 8, 116);
    bool wifiConnected = WiFi.status() == WL_CONNECTED;
    spr.setTextColor(wifiConnected ? TFT_GREEN : TFT_RED, TFT_BLACK);
    spr.drawString(wifiConnected ? fitHeaderText(WiFi.SSID(), 30) : "offline", 8, 138);
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawString("IP:", 8, 164);
    spr.setTextColor(TFT_CYAN, TFT_BLACK);
    spr.drawString(wifiConnected ? fitHeaderText(WiFi.localIP().toString(), 30) : "no ip", 8, 186);
    spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
    bool sdConfirmActive = sdResetConfirmPending && millis() <= sdResetConfirmUntilMs;
    unsigned long sdConfirmLeft = sdConfirmActive ? ((sdResetConfirmUntilMs - millis()) / 1000UL) : 0;
    spr.setTextColor(sdConfirmActive ? TFT_ORANGE : TFT_DARKGREY, TFT_BLACK);
    if (sdConfirmActive) {
        spr.drawString("PRESS again: confirm SD reset (" + String(sdConfirmLeft) + "s)", 8, 198);
    } else {
        spr.drawString("PRESS: arm SD reset (2-click)", 8, 198);
    }

    bool showSdStatus = millis() <= sdResetStatusUntilMs;
    spr.setTextColor(showSdStatus ? (sdResetLastSuccess ? TFT_GREEN : TFT_RED) : TFT_DARKGREY, TFT_BLACK);
    spr.drawString(showSdStatus ? (sdResetLastSuccess ? "SD reset done" : "SD reset failed") : "RIGHT: toggle ack verify", 8, 208);
    spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
    spr.drawString("UP/DOWN: BLE window 30/60/120s", 8, 218);
    spr.drawString("LEFT: Menu", 8, 228);
    drawBleUnlockBadge();
    spr.pushSprite(0, 0);
}

void sendToScreen() {
    spr.fillSprite(TFT_BLACK);
    spr.fillRect(0, 0, 320, 55, TFT_PROCOMSABLUE);
    spr.setTextColor(TFT_WHITE);
    spr.setFreeFont(FSSO12);
    spr.drawString("Termocheck", 50, 10);
    spr.setFreeFont(FSSO9);
    String location = "GGG-" + String(placeCharacteristic->getValue().c_str());
    spr.drawString(location, 50, 35);
    if (tnow == "") {
        now = rtc.now();
    }
    char fmt[] = "MMM DD hh:mm";
    tnow = now.toString(fmt);
    spr.drawString(tnow, 210, 10);
    String sLight = "Luz: " + String(light) + " %";
    spr.drawString(sLight, 230, 35);

    spr.drawFastVLine(160, 55, 95, TFT_PROCOMSABLUE);
    spr.drawFastHLine(0, 150, 320, TFT_PROCOMSABLUE);

    spr.setFreeFont(FSSO12);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("Temp C", 35, 63);
    spr.setFreeFont(FSSO24);
    spr.drawFloat(aTemperature, 2, 20, 100);

    spr.setFreeFont(FSSO12);
    spr.drawString("Hum Rel %", 180, 63);
    spr.setFreeFont(FSSO24);
    spr.drawFloat(aHumidity, 2, 180, 100);

    // WiFi status at bottom
    bool wifiConnected = WiFi.status() == WL_CONNECTED;
    drawWiFiIconSmall(15, 225, wifiConnected);
    spr.setTextColor(wifiConnected ? TFT_GREEN : TFT_RED);
    spr.setTextFont(1);
    String wifiFailureReason = "";
    if (!wifiConnected) {
        wifiFailureReason = fitHeaderText(getLastWiFiFailureReason(), 40);
        spr.drawString(wifiFailureReason, 8, 206);
    }
    String wifiName = wifiConnected ? fitHeaderText(WiFi.SSID(), 18) : "offline";
    String wifiIp = wifiConnected ? WiFi.localIP().toString() : "no ip";
    spr.drawString(wifiName, 28, 220);
    spr.drawString(fitHeaderText(wifiIp, 20), 28, 232);

    drawBleUnlockBadge();

    spr.pushSprite(0, 0);

    logMainScreenSnapshot(location, tnow, aTemperature, aHumidity, light, wifiName, wifiIp, wifiConnected, wifiFailureReason);
}
