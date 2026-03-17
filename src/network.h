#ifndef NETWORK_H
#define NETWORK_H

#include <ArduinoJson.h>

unsigned long sendNTPpacket(const char* address);
unsigned long getNTPtime();
void scanNetworks(DynamicJsonDocument* jsonDoc);
void connectWiFi();
bool ensureWiFiConnected();
int sendPostMessage(DynamicJsonDocument* document);

#endif
