#ifndef APP_STATE_H
#define APP_STATE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <rpcWiFi.h>
#include "RTC_SAMD51.h"
#include "SensirionI2cSht4x.h"
#if __has_include(<Seeed_SHT35.h>)
#include <Seeed_SHT35.h>
#else
#include "../.pio/libdeps/seeed_wio_terminal/Grove - I2C High Accuracy Temp_Humi Sensor SHT35/Seeed_SHT35.h"
#endif
#include "TFT_eSPI.h"
#include "SD/Seeed_SD.h"
#include <rpcBLEDevice.h>

extern BLEDescriptor* pDescriptor;
extern BLECharacteristic* temperatureCharacteristic;
extern BLECharacteristic* humidityCharacteristic;
extern BLECharacteristic* placeCharacteristic;

extern TFT_eSPI tft;
extern TFT_eSprite spr;

extern String strBaseMac;
extern const char* ssid;
extern const char* password;
extern const char* alternatessid;
extern const char* alternatepassword;
extern const char* mobilessid;
extern int wifissid;
extern int channels[11];
extern float distance;

extern unsigned long currentMillis;
extern unsigned long previousMillisC;

extern SensirionI2cSht4x sensor;
extern SHT35 sensor35;
extern char errorMessage[64];
extern int16_t error;
extern uint32_t serialNumber;
extern float aTemperature;
extern float aHumidity;
extern int light;

extern unsigned int localPort;
extern char timeServer[];
extern byte packetBuffer[];
extern DateTime now;
extern WiFiUDP udp;
extern RTC_SAMD51 rtc;
extern String tnow;

extern File LogFile;
extern bool syslogCreated;
extern bool unsentlogCreated;

extern const char* root_ca;

#endif
