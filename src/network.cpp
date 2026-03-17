#include "network.h"

#include <Arduino.h>
#include <rpcWiFi.h>
#include <HTTPClient.h>

#if __has_include("secrets_local.h")
#include "secrets_local.h"
#else
#include "secrets_template.h"
#endif

#include "app_state.h"

#define SERVERURL API_SERVER_URL
#define K 27.55
#define BEARERTOKEN API_BEARER_TOKEN

static const int kNtpPacketSize = 48;
static const unsigned long kWifiInitialBackoffMs = 1000;
static const unsigned long kWifiMaxBackoffMs = 60000;
static const unsigned long kWifiConnectWindowMs = 10000;

static unsigned long sLastWifiAttemptMs = 0;
static unsigned long sWifiBackoffMs = kWifiInitialBackoffMs;

unsigned long sendNTPpacket(const char* address) {
    for (int i = 0; i < kNtpPacketSize; ++i) {
        packetBuffer[i] = 0;
    }
    packetBuffer[0] = 0b11100011;
    packetBuffer[1] = 0;
    packetBuffer[2] = 6;
    packetBuffer[3] = 0xEC;
    packetBuffer[12] = 49;
    packetBuffer[13] = 0x4E;
    packetBuffer[14] = 49;
    packetBuffer[15] = 52;

    udp.beginPacket(address, 123);
    udp.write(packetBuffer, kNtpPacketSize);
    udp.endPacket();
    return 0;
}

unsigned long getNTPtime() {
    if (WiFi.status() == WL_CONNECTED) {
        udp.begin(WiFi.localIP(), localPort);

        sendNTPpacket(timeServer);
        delay(1000);

        if (udp.parsePacket()) {
            Serial.println("udp packet received");
            Serial.println("");
            udp.read(packetBuffer, kNtpPacketSize);

            unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
            unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
            unsigned long secsSince1900 = highWord << 16 | lowWord;
            const unsigned long seventyYears = 2208988800UL;
            unsigned long epoch = secsSince1900 - seventyYears;
            long tzOffset = -21600UL;
            unsigned long adjustedTime;
            return adjustedTime = epoch + tzOffset;
        } else {
            udp.stop();
            return 0;
        }
        udp.stop();
    } else {
        return 0;
    }
}

void scanNetworks(DynamicJsonDocument* jsonDoc) {
    Serial.println("scan start");

    if (WiFi.status() == WL_DISCONNECTED) {
        int n = WiFi.scanNetworks();
        Serial.println("scan done");
        if (n == 0) {
            Serial.println("no networks found");
        } else {
            Serial.print(n);
            Serial.println(" networks found");
            for (int i = 0; i < n; ++i) {
                distance = pow(10, ((K - (20 * log10(channels[WiFi.channel(i) - 1])) + abs(WiFi.RSSI(i))) / 20));

                (*jsonDoc).createNestedObject("ap" + String(i));
                (*jsonDoc)["ap" + String(i)]["name"] = WiFi.SSID(i);
                (*jsonDoc)["ap" + String(i)]["signal"] = WiFi.RSSI(i);
                (*jsonDoc)["ap" + String(i)]["channel"] = WiFi.channel(i);
                (*jsonDoc)["ap" + String(i)]["distance"] = distance;
                (*jsonDoc)["ap" + String(i)]["encriptionType"] = WiFi.encryptionType(i);
                (*jsonDoc)["ap" + String(i)]["sensorHostMAC"] = strBaseMac;

                if (WiFi.SSID(i) == ssid) {
                    if (wifissid == 2) {
                        wifissid = 3;
                    } else {
                        wifissid = 1;
                    }
                } else if (WiFi.SSID(i) == alternatessid) {
                    if (wifissid == 1) {
                        wifissid = 3;
                    } else {
                        wifissid = 2;
                    }
                } else {
                    if (WiFi.SSID(i) == mobilessid) {
                        wifissid = 4;
                    } else if (wifissid == 0) {
                        wifissid = 5;
                    }
                }
            }
        }
    }
}

void connectWiFi() {
    Serial.print("wifissid: ");
    Serial.println(String(wifissid));

    const char* issid = "";
    const char* ipwd = "";

    if (wifissid != 1 && wifissid != 2 && wifissid != 3) {
        Serial.print("Error Rescanning Networks ");
        DynamicJsonDocument scanData(4096);
        scanNetworks(&scanData);
    } else {
        if (WiFi.status() == WL_DISCONNECTED) {
            if (wifissid == 1 || wifissid == 3) {
                issid = ssid;
                ipwd = password;
                Serial.print("Primary Wifi connecting ");
            } else if (wifissid == 2) {
                issid = alternatessid;
                ipwd = alternatepassword;
                Serial.print("Alternate Wifi connecting ");
            }

            WiFi.begin(issid, ipwd);
            while (WiFi.status() != WL_CONNECTED) {
                currentMillis = millis();
                if (currentMillis - previousMillisC >= 500) {
                    previousMillisC = currentMillis;
                    Serial.print(".");
                    WiFi.begin(issid, ipwd);
                }
            }
            Serial.println("");
            Serial.println("WiFi connected.");
            Serial.print("IP address: ");
            Serial.println(WiFi.localIP());
        }
    }
}

bool ensureWiFiConnected() {
    if (WiFi.status() == WL_CONNECTED) {
        sWifiBackoffMs = kWifiInitialBackoffMs;
        return true;
    }

    unsigned long nowMs = millis();
    if (nowMs - sLastWifiAttemptMs < sWifiBackoffMs) {
        return false;
    }
    sLastWifiAttemptMs = nowMs;

    if (wifissid != 1 && wifissid != 2 && wifissid != 3) {
        DynamicJsonDocument scanData(4096);
        scanNetworks(&scanData);
    }

    const char* issid = nullptr;
    const char* ipwd = nullptr;
    if (wifissid == 1 || wifissid == 3) {
        issid = ssid;
        ipwd = password;
    } else if (wifissid == 2) {
        issid = alternatessid;
        ipwd = alternatepassword;
    } else {
        sWifiBackoffMs = min(kWifiMaxBackoffMs, sWifiBackoffMs * 2);
        return false;
    }

    Serial.print("WiFi reconnect attempt with backoff(ms): ");
    Serial.println(String(sWifiBackoffMs));
    WiFi.begin(issid, ipwd);

    unsigned long windowStart = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - windowStart) < kWifiConnectWindowMs) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi reconnected.");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        sWifiBackoffMs = kWifiInitialBackoffMs;
        return true;
    }

    WiFi.disconnect();
    sWifiBackoffMs = min(kWifiMaxBackoffMs, sWifiBackoffMs * 2);
    return false;
}

int sendPostMessage(DynamicJsonDocument* document) {
    if (WiFi.status() == WL_CONNECTED) {
        String message = "";
        serializeJson(*document, message);

        HTTPClient http;
        http.setReuse(false);
        Serial.println("Begin Connection...");
        int svrResponse = http.begin(SERVERURL, root_ca);
        Serial.println("Begin response:");
        Serial.println(svrResponse);
        http.addHeader("Content-Type", "application/json; charset=utf-8");
        http.addHeader("Authorization", "Bearer " + String(BEARERTOKEN));

        Serial.println("Sending Post message...");
        int httpResponseCode = http.POST(message);
        Serial.println("Post Message Sent and received response...");

        Serial.println("Response Code: " + String(httpResponseCode));
        if (httpResponseCode > 0) {
            String response = http.getString();
            Serial.println(httpResponseCode);
            Serial.println(response);
        } else {
            Serial.print("Error on sending Information to Server: ");
            Serial.println(httpResponseCode);
            http.end();
            return 1;
        }
        http.end();
    } else {
        Serial.println("Error in WiFi connection");
    }
    return 0;
}
