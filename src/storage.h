#ifndef STORAGE_H
#define STORAGE_H

#include <ArduinoJson.h>

void writeDataLogFile(DynamicJsonDocument* jsonDoc, bool unSent);

#endif
