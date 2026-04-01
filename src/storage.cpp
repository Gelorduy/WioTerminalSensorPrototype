#include "storage.h"

#include <Arduino.h>
#include "SD/Seeed_SD.h"
#include "app_state.h"
#include "network.h"

static const size_t kMaxLogSizeBytes = 128 * 1024;
static const unsigned long kQueueRetryCooldownMs = 15000UL;
static unsigned long sQueueRetryBlockedUntilMs = 0;
static unsigned long sQueueRetryLastNoticeMs = 0;

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

    if (maxEntries == 0) {
        return false;
    }

    unsigned long nowMs = millis();
    if (sQueueRetryBlockedUntilMs != 0 && (long)(sQueueRetryBlockedUntilMs - nowMs) > 0) {
        if (sQueueRetryLastNoticeMs == 0 || (nowMs - sQueueRetryLastNoticeMs) >= 2000UL) {
            appendEventLog("queue: retry cooldown active");
            sQueueRetryLastNoticeMs = nowMs;
        }
        return false;
    }
    sQueueRetryLastNoticeMs = 0;

    String unsentName = getUnsentLogName();
    if (!SD.exists(unsentName)) {
        unsentlogCreated = false;
        return true;
    }

    // Preflight: confirm at least one payload can be posted successfully.
    // If server is down, exit early and avoid reprocessing/rewriting the whole queue.
    String preflightRawLine = "";
    String preflightConfirmedLine = "";
    bool hasSendablePayload = false;
    {
        File preflightFile = SD.open(unsentName, FILE_READ);
        if (!preflightFile) {
            Serial.println("Failed to open pending queue for preflight.");
            return false;
        }

        while (preflightFile.available()) {
            String line = preflightFile.readStringUntil('\n');
            line.trim();
            if (line.length() == 0 || line == "{" || line == "},") {
                continue;
            }

            DynamicJsonDocument preflightDoc(4096);
            auto preflightErr = deserializeJson(preflightDoc, line);
            if (preflightErr) {
                continue;
            }

            hasSendablePayload = true;
            int preflightRes = sendPostMessage(&preflightDoc);
            if (preflightRes != 200) {
                appendEventLog("queue: preflight failed, post code=" + String(preflightRes));
                sQueueRetryBlockedUntilMs = millis() + kQueueRetryCooldownMs;
                preflightFile.close();
                return false;
            }

            preflightRawLine = line;
            serializeJson(preflightDoc, preflightConfirmedLine);
            appendEventLog("queue: preflight confirmed payload");
            break;
        }
        preflightFile.close();
    }

    if (!hasSendablePayload) {
        SD.remove(unsentName);
        unsentlogCreated = false;
        appendEventLog("queue: drained");
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

    const unsigned long kQueueProcessBudgetMs = 250UL;
    const unsigned long processStartMs = millis();
    size_t sentCount = 1;
    size_t attemptCount = 1;
    bool allConfirmed = true;
    bool preflightLineRemoved = false;
    while (inFile.available()) {
        String line = inFile.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line == "{" || line == "},") {
            continue;
        }

        if (!preflightLineRemoved && (line == preflightRawLine || line == preflightConfirmedLine)) {
            // Drop the payload already confirmed by preflight to avoid duplicate posts.
            preflightLineRemoved = true;
            continue;
        }

        bool budgetExceeded = (millis() - processStartMs) >= kQueueProcessBudgetMs;
        if (attemptCount >= maxEntries || budgetExceeded) {
            // Keep untouched lines as-is to avoid expensive parse/serialize work.
            outFile.println(line);
            allConfirmed = false;
            if (budgetExceeded) {
                appendEventLog("queue: paused by time budget");
            }
            yield();
            continue;
        }

        DynamicJsonDocument doc(4096);
        auto err = deserializeJson(doc, line);
        if (err) {
            outFile.println(line);
            allConfirmed = false;
            yield();
            continue;
        }

        attemptCount++;
        int postRes = sendPostMessage(&doc);
        if (postRes == 200) {
            sentCount++;
            appendEventLog("queue: confirmed payload");
            yield();
            continue;
        }
        appendEventLog("queue: keep pending, post code=" + String(postRes));
        sQueueRetryBlockedUntilMs = millis() + kQueueRetryCooldownMs;

        String keepLine = "";
        serializeJson(doc, keepLine);
        outFile.println(keepLine);
        allConfirmed = false;
        yield();
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
    if (allConfirmed) {
        sQueueRetryBlockedUntilMs = 0;
    }
    return false;
}

void appendEventLog(const String& message) {
    if (message.length() == 0) {
        return;
    }

    // Keep event logs on serial only. SD.open can block indefinitely on flaky cards
    // and freeze the main loop in hot paths (including HTTPS upload).
    Serial.println("EVT: " + message);
}

void saveBlePlaceName(const String& name) {
    if (!sdcard) {
        return;
    }

    String fileName = "ble_place_" + String(serialNumber) + ".txt";
    File file = SD.open(fileName, FILE_WRITE);
    if (!file) {
        Serial.println("Error opening " + fileName + " for write");
        return;
    }

    file.println(name);
    file.close();
    Serial.println("Saved BLE place name: " + name);
}

String loadBlePlaceName() {
    if (!sdcard) {
        return "Anywhere";
    }

    String fileName = "ble_place_" + String(serialNumber) + ".txt";
    if (!SD.exists(fileName)) {
        return "Anywhere";
    }

    File file = SD.open(fileName, FILE_READ);
    if (!file) {
        return "Anywhere";
    }

    String name = "";
    while (file.available()) {
        char c = file.read();
        if (c == '\n' || c == '\r') {
            break;
        }
        name += c;
    }
    file.close();

    if (name.length() == 0) {
        return "Anywhere";
    }

    Serial.println("Loaded BLE place name: " + name);
    return name;
}

bool reinitializeSdCardFiles() {
    if (!sdcard) {
        Serial.println("SD reinit skipped: card unavailable");
        return false;
    }

    bool ok = true;
    const String sn = String(serialNumber);
    const String filesToDelete[] = {
        "readings" + sn + ".log",
        "readings" + sn + ".log.bak",
        "unsent" + sn + ".log",
        "unsent" + sn + ".log.bak",
        "unsent" + sn + ".log.tmp",
        "events" + sn + ".log",
        "events" + sn + ".log.bak",
        "ble_place_" + sn + ".txt"
    };

    for (size_t i = 0; i < (sizeof(filesToDelete) / sizeof(filesToDelete[0])); ++i) {
        const String& fileName = filesToDelete[i];
        if (!SD.exists(fileName)) {
            continue;
        }
        if (!SD.remove(fileName)) {
            Serial.println("SD reinit failed removing: " + fileName);
            ok = false;
        }
    }

    syslogCreated = SD.exists("readings" + sn + ".log");
    unsentlogCreated = SD.exists("unsent" + sn + ".log");
    Serial.println(ok ? "SD reinit complete" : "SD reinit partial failure");
    return ok;
}
