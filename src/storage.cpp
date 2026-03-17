#include "storage.h"

#include <Arduino.h>
#include "SD/Seeed_SD.h"
#include "app_state.h"

void writeDataLogFile(DynamicJsonDocument* jsonDoc, bool unSent) {
    String logName = "readings" + String(serialNumber) + ".log";
    bool logCreated = syslogCreated;

    if (unSent) {
        logName = "unsent" + String(serialNumber) + ".log";
        logCreated = unsentlogCreated;
    }
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
