#include "storage.h"

#include <Arduino.h>
#include "SD/Seeed_SD.h"
#include "app_state.h"
#include "network.h"

static const size_t kMaxLogSizeBytes = 128 * 1024;

static String getUnsentLogName() {
    return "unsent" + String(serialNumber) + ".log";
}

static void rotateLogIfNeeded(const String& logName) {
    if (!SD.exists(logName)) {
        return;
    }

    File existing = SD.open(logName, FILE_READ);
    if (!existing) {
        return;
    }
    size_t currentSize = existing.size();
    existing.close();

    if (currentSize < kMaxLogSizeBytes) {
        return;
    }

    String backupName = logName + ".bak";
    if (SD.exists(backupName)) {
        SD.remove(backupName);
    }
    SD.rename(logName, backupName);
    Serial.println("Rotated log file: " + logName);
}

void writeDataLogFile(DynamicJsonDocument* jsonDoc, bool unSent) {
    String logName = "readings" + String(serialNumber) + ".log";
    bool logCreated = syslogCreated;

    if (unSent) {
        logName = "unsent" + String(serialNumber) + ".log";
        logCreated = unsentlogCreated;
    }

    rotateLogIfNeeded(logName);
    Serial.println("Opening file: " + logName);

    if (logCreated) {
        LogFile = SD.open(logName, FILE_APPEND);
        Serial.println("Appending file: " + logName);
    } else {
        LogFile = SD.open(logName, FILE_WRITE);
        Serial.println("Creating file: " + logName);
        if (unSent) {
            unsentlogCreated = true;
        } else {
            syslogCreated = true;
        }
    }

    if (LogFile) {
        Serial.println("Writing to LogFile File: " + logName);
        String message = "";
        serializeJson(*jsonDoc, message);
        LogFile.println("{");
        LogFile.print(message);
        LogFile.println("},");
        LogFile.close();
        Serial.println("done.");
    } else {
        Serial.println("error opening LogFile" + logName);
    }
}

bool resendUnsentLogs(size_t maxEntries) {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    String unsentName = getUnsentLogName();
    if (!SD.exists(unsentName)) {
        unsentlogCreated = false;
        return true;
    }

    File inFile = SD.open(unsentName, FILE_READ);
    if (!inFile) {
        Serial.println("Failed to open unsent log for reading.");
        return false;
    }

    String tempName = unsentName + ".tmp";
    SD.remove(tempName);
    File outFile = SD.open(tempName, FILE_WRITE);
    if (!outFile) {
        inFile.close();
        Serial.println("Failed to open temp unsent log for writing.");
        return false;
    }

    size_t sentCount = 0;
    bool allSent = true;
    while (inFile.available()) {
        String line = inFile.readStringUntil('\n');
        line.trim();

        if (line.length() == 0 || line == "{" || line == "},") {
            continue;
        }

        DynamicJsonDocument doc(4096);
        auto err = deserializeJson(doc, line);
        if (err) {
            outFile.println(line);
            allSent = false;
            continue;
        }

        if (sentCount < maxEntries) {
            int postRes = sendPostMessage(&doc);
            if (postRes == 0) {
                sentCount++;
                continue;
            }
        }

        String keepLine = "";
        serializeJson(doc, keepLine);
        outFile.println(keepLine);
        allSent = false;
    }

    inFile.close();
    outFile.close();

    SD.remove(unsentName);
    File compacted = SD.open(tempName, FILE_READ);
    size_t remaining = compacted ? compacted.size() : 0;
    if (compacted) {
        compacted.close();
    }

    if (remaining == 0) {
        SD.remove(tempName);
        unsentlogCreated = false;
        Serial.println("Unsent queue drained.");
        return allSent;
    }

    SD.rename(tempName, unsentName);
    unsentlogCreated = true;
    Serial.println("Unsent queue retained entries.");
    return false;
}
