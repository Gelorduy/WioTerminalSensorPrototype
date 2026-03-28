#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>

void beginStartupStatus();
void logStartupStatus(const String& message);
void endStartupStatus();
void sendToScreen();
void sendMenuScreen(int selectedIndex);
void sendLogMenuScreen(int selectedIndex);
void sendLogViewerScreen(bool eventsLog, int scrollOffset);
void sendConfigScreen();

#endif
