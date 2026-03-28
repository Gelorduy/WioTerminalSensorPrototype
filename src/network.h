#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>
#include <ArduinoJson.h>

typedef void (*WiFiStatusCallback)(const String& message);

unsigned long sendNTPpacket(const char* address);
unsigned long getNTPtime();
bool syncRtcFromNtp();
void scanNetworks(DynamicJsonDocument* jsonDoc);
void connectWiFi();
bool ensureWiFiConnected();
void setWiFiStatusCallback(WiFiStatusCallback callback);
String getLastWiFiFailureReason();
String getLastWiFiTargetSummary();
String wifiStatusToString(int status);
int sendPostMessage(DynamicJsonDocument* document);

#endif
