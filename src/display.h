#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>

void beginStartupStatus();
void logStartupStatus(const String& message);
void endStartupStatus();
void sendToScreen();

#endif
