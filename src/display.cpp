#include "display.h"

#include <Arduino.h>
#include "Free_Fonts.h"
#include "app_state.h"

#define TFT_PROCOMSABLUE 0x2A51

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

void sendToScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, 320, 55, TFT_PROCOMSABLUE);
    tft.setTextColor(TFT_WHITE);
    tft.setFreeFont(FSSO12);
    tft.drawString("Termocheck", 50, 10);
    tft.setFreeFont(FSSO9);
    String location = "GGG-" + String(placeCharacteristic->getValue().c_str());
    tft.drawString(location, 50, 35);
    if (tnow == "") {
        now = rtc.now();
    }
    char fmt[] = "MMM DD hh:mm";
    tnow = now.toString(fmt);
    tft.drawString(tnow, 210, 10);
    String sLight = "Luz: " + String(light) + " %";
    tft.drawString(sLight, 230, 35);

    tft.drawFastVLine(160, 55, 95, TFT_PROCOMSABLUE);
    tft.drawFastHLine(0, 150, 320, TFT_PROCOMSABLUE);

    tft.setFreeFont(FSSO12);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Temp C", 35, 63);
    tft.setFreeFont(FSSO24);
    tft.drawFloat(aTemperature, 2, 20, 100);

    tft.setFreeFont(FSSO12);
    tft.drawString("Hum Rel %", 180, 63);
    tft.setFreeFont(FSSO24);
    tft.drawFloat(aHumidity, 2, 180, 100);

}
