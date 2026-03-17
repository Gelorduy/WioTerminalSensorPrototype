#ifndef SENSORS_H
#define SENSORS_H

#include <ArduinoJson.h>

void getEnvironmentData(DynamicJsonDocument* jsonDoc, int sensorType = 40);

#endif
