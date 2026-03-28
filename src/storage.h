#ifndef STORAGE_H
#define STORAGE_H

#include <ArduinoJson.h>

void writeDataLogFile(DynamicJsonDocument* jsonDoc, bool unSent);
bool resendUnsentLogs(size_t maxEntries = 10);
bool enqueuePostForRetry(DynamicJsonDocument* jsonDoc);
bool processPendingPosts(size_t maxEntries = 1);
void appendEventLog(const String& message);

#endif
