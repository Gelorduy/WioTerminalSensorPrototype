#include "storage.h"

#include <Arduino.h>
#include "SD/Seeed_SD.h"
#include "app_state.h"
#include "network.h"

static const size_t kMaxLogSizeBytes = 128 * 1024;

static String getUnsentLogName() {
    return "unsent" + String(serialNumber) + ".log";
}

static String getEventsLogName() {
    return "events" + String(serialNumber) + ".log";
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
    return processPendingPosts(maxEntries);
}

bool enqueuePostForRetry(DynamicJsonDocument* jsonDoc) {
    if (!sdcard || jsonDoc == nullptr) {
        return false;
    }

    String unsentName = getUnsentLogName();
    rotateLogIfNeeded(unsentName);

    File queueFile = SD.open(unsentName, FILE_APPEND);
    if (!queueFile) {
        queueFile = SD.open(unsentName, FILE_WRITE);
    }
    if (!queueFile) {
        Serial.println("Failed to open pending post queue: " + unsentName);
        return false;
    }

    String message = "";
    serializeJson(*jsonDoc, message);
    queueFile.println(message);
    queueFile.close();
    unsentlogCreated = true;
    appendEventLog("queue: enqueued payload");
    return true;
}

bool processPendingPosts(size_t maxEntries) {
    if (!sdcard || WiFi.status() != WL_CONNECTED) {
        return false;
    }

    String unsentName = getUnsentLogName();
    if (!SD.exists(unsentName)) {
        unsentlogCreated = false;
        return true;
    }

    File inFile = SD.open(unsentName, FILE_READ);
    if (!inFile) {
        Serial.println("Failed to open pending queue for reading.");
        return false;
    }

    String tempName = unsentName + ".tmp";
    SD.remove(tempName);
    File outFile = SD.open(tempName, FILE_WRITE);
    if (!outFile) {
        inFile.close();
        Serial.println("Failed to open pending temp queue for writing.");
        return false;
    }

    size_t sentCount = 0;
    bool allConfirmed = true;
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
            allConfirmed = false;
            continue;
        }

        if (sentCount < maxEntries) {
            int postRes = sendPostMessage(&doc);
            if (postRes == 200) {
                sentCount++;
                appendEventLog("queue: confirmed payload");
                continue;
            }
            appendEventLog("queue: keep pending, post code=" + String(postRes));
        }

        String keepLine = "";
        serializeJson(doc, keepLine);
        outFile.println(keepLine);
        allConfirmed = false;
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
        appendEventLog("queue: drained");
        return allConfirmed;
    }

    SD.rename(tempName, unsentName);
    unsentlogCreated = true;
    return false;
}

void appendEventLog(const String& message) {
    if (!sdcard || message.length() == 0) {
        return;
    }

    String logName = getEventsLogName();
    rotateLogIfNeeded(logName);

    File eventFile = SD.open(logName, FILE_APPEND);
    if (!eventFile) {
        eventFile = SD.open(logName, FILE_WRITE);
    }

    if (!eventFile) {
        Serial.println("error opening events log " + logName);
        return;
    }

    eventFile.println(message);
    eventFile.close();
}
