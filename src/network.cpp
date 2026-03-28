#include "network.h"

#include <Arduino.h>
#include <rpcWiFi.h>
#include <HTTPClient.h>
#include "storage.h"

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
static const unsigned long kNtpResponseTimeoutMs = 1500;
static const unsigned long kWifiInitialBackoffMs = 1000;
static const unsigned long kWifiMaxBackoffMs = 60000;
static const unsigned long kWifiConnectWindowMs = 10000;
static const unsigned long kWifiRescanIntervalMs = 15000;

static unsigned long sLastWifiAttemptMs = 0;
static unsigned long sWifiBackoffMs = kWifiInitialBackoffMs;
static String sLastWifiFailureReason = "not attempted";
static String sLastWiFiTargetSummary = "target not scanned";
static unsigned long sLastBackoffNoticeMs = 0;
static unsigned long sLastAttemptStatusReportMs = 0;
static WiFiStatusCallback sWiFiStatusCallback = nullptr;
static bool sPrimaryVisible = false;
static bool sAlternateVisible = false;
static bool sMobileVisible = false;
static bool sWifiAttemptInProgress = false;
static int sNextNetworkIndex = 0;
static int sActiveNetworkIndex = -1;
static unsigned long sAttemptWindowStartMs = 0;
static String sLastAttemptFailure = "";
static String sPrimaryTargetInfo = "";
static String sAlternateTargetInfo = "";
static String sMobileTargetInfo = "";
static unsigned long sLastScanMs = 0;

static void reportWiFiStatus(const String& message) {
    Serial.println(message);
    if (sWiFiStatusCallback != nullptr) {
        sWiFiStatusCallback(message);
    }
}

void setWiFiStatusCallback(WiFiStatusCallback callback) {
    sWiFiStatusCallback = callback;
}

static String wifiStatusHint(int status) {
    switch (status) {
        case WL_NO_SSID_AVAIL:
            return "target AP missing";
        case WL_CONNECT_FAILED:
            return "check password/auth mode";
        case WL_CONNECTION_LOST:
            return "weak signal or AP reset";
        case WL_DISCONNECTED:
            return "association rejected";
        default:
            return "retrying";
    }
}

static String buildTargetSummary() {
    String summary = "visible: ";
    bool first = true;

    if (sPrimaryVisible) {
        summary += sPrimaryTargetInfo;
        first = false;
    }
    if (sAlternateVisible) {
        if (!first) summary += " | ";
        summary += sAlternateTargetInfo;
        first = false;
    }
    if (sMobileVisible) {
        if (!first) summary += " | ";
        summary += sMobileTargetInfo;
        first = false;
    }

    if (first) {
        return "visible: none of configured SSIDs";
    }
    return summary;
}

static String encryptionTypeToString(int encryptionType) {
    return "enc:" + String(encryptionType);
}

String wifiStatusToString(int status) {
    switch (status) {
        case WL_IDLE_STATUS:
            return "idle";
        case WL_NO_SSID_AVAIL:
            return "no ssid avail";
        case WL_SCAN_COMPLETED:
            return "scan completed";
        case WL_CONNECTED:
            return "connected";
        case WL_CONNECT_FAILED:
            return "connect failed";
        case WL_CONNECTION_LOST:
            return "connection lost";
        case WL_DISCONNECTED:
            return "disconnected";
        default:
            return "status " + String(status);
    }
}

String getLastWiFiFailureReason() {
    return sLastWifiFailureReason;
}

String getLastWiFiTargetSummary() {
    return sLastWiFiTargetSummary;
}

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

static unsigned long getNtpTimeFromServer(const char* serverAddress) {
    if (WiFi.status() != WL_CONNECTED) {
        return 0;
    }

    udp.stop();
    udp.begin(localPort);
    sendNTPpacket(serverAddress);

    unsigned long startMs = millis();
    while ((millis() - startMs) < kNtpResponseTimeoutMs) {
        int packetSize = udp.parsePacket();
        if (packetSize >= kNtpPacketSize) {
            udp.read(packetBuffer, kNtpPacketSize);

            unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
            unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
            unsigned long secsSince1900 = (highWord << 16) | lowWord;
            const unsigned long seventyYears = 2208988800UL;
            unsigned long epoch = secsSince1900 - seventyYears;
            long tzOffset = -21600UL;
            udp.stop();
            return epoch + tzOffset;
        }
        delay(50);
    }

    udp.stop();
    return 0;
}

unsigned long getNTPtime() {
    if (WiFi.status() != WL_CONNECTED) {
        return 0;
    }

    const char* ntpServers[] = {
        timeServer,
        "pool.ntp.org",
        "time.windows.com",
        "time.cloudflare.com"
    };

    for (size_t i = 0; i < 4; ++i) {
        unsigned long epoch = getNtpTimeFromServer(ntpServers[i]);
        if (epoch > 0) {
            appendEventLog("ntp: sync source=" + String(ntpServers[i]) + " epoch=" + String(epoch));
            return epoch;
        }
        appendEventLog("ntp: no response from " + String(ntpServers[i]));
    }

    return 0;
}

bool syncRtcFromNtp() {
    unsigned long epoch = getNTPtime();
    if (epoch == 0) {
        appendEventLog("ntp: sync failed");
        return false;
    }

    rtc.adjust(DateTime(epoch));
    now = rtc.now();
    tnow = "";
    appendEventLog("ntp: rtc adjusted " + now.timestamp(DateTime::TIMESTAMP_FULL));
    reportWiFiStatus("NTP sync ok");
    return true;
}

void scanNetworks(DynamicJsonDocument* jsonDoc) {
    Serial.println("scan start");
    sLastWiFiTargetSummary = "target not visible";
    sPrimaryVisible = false;
    sAlternateVisible = false;
    sMobileVisible = false;
    sPrimaryTargetInfo = "";
    sAlternateTargetInfo = "";
    sMobileTargetInfo = "";

    if (WiFi.status() != WL_CONNECTED) {
        sLastScanMs = millis();
        int n = WiFi.scanNetworks();
        Serial.println("scan done");
        if (n == 0) {
            Serial.println("no networks found");
        } else {
            Serial.print(n);
            Serial.println(" networks found");
            Serial.println("--- WiFi Scan Results ---");
            for (int i = 0; i < n; ++i) {
                int channel = WiFi.channel(i);
                int channelIndex = channel - 1;
                int frequencyMhz = 2412;
                if (channelIndex >= 0 && channelIndex < 11) {
                    frequencyMhz = channels[channelIndex];
                } else if (channel >= 1) {
                    frequencyMhz = 2412 + ((channel - 1) * 5);
                }
                Serial.print("  [" + String(i) + "] SSID: \"");
                Serial.print(WiFi.SSID(i));
                Serial.print("\"  RSSI: ");
                Serial.print(WiFi.RSSI(i));
                Serial.print(" dBm  Ch: ");
                Serial.print(channel);
                bool isTarget = (WiFi.SSID(i) == ssid || WiFi.SSID(i) == alternatessid || WiFi.SSID(i) == mobilessid);
                if (isTarget) Serial.print("  << TARGET");
                Serial.println();
                distance = pow(10, ((K - (20 * log10(frequencyMhz)) + abs(WiFi.RSSI(i))) / 20));

                (*jsonDoc).createNestedObject("ap" + String(i));
                (*jsonDoc)["ap" + String(i)]["name"] = WiFi.SSID(i);
                (*jsonDoc)["ap" + String(i)]["signal"] = WiFi.RSSI(i);
                (*jsonDoc)["ap" + String(i)]["channel"] = channel;
                (*jsonDoc)["ap" + String(i)]["distance"] = distance;
                (*jsonDoc)["ap" + String(i)]["encriptionType"] = WiFi.encryptionType(i);
                (*jsonDoc)["ap" + String(i)]["sensorHostMAC"] = strBaseMac;

                if (WiFi.SSID(i) == ssid) {
                    sPrimaryVisible = true;
                    sPrimaryTargetInfo = String(ssid) + " rssi " + String(WiFi.RSSI(i)) + " ch " + String(channel) + " " + encryptionTypeToString(WiFi.encryptionType(i));
                } else if (WiFi.SSID(i) == alternatessid) {
                    sAlternateVisible = true;
                    sAlternateTargetInfo = String(alternatessid) + " rssi " + String(WiFi.RSSI(i)) + " ch " + String(channel) + " " + encryptionTypeToString(WiFi.encryptionType(i));
                } else {
                    if (WiFi.SSID(i) == mobilessid) {
                        sMobileVisible = true;
                        sMobileTargetInfo = String(mobilessid) + " rssi " + String(WiFi.RSSI(i)) + " ch " + String(channel) + " " + encryptionTypeToString(WiFi.encryptionType(i));
                    }
                }
            }

            if (sPrimaryVisible && sAlternateVisible) {
                wifissid = 3;
            } else if (sPrimaryVisible) {
                wifissid = 1;
            } else if (sAlternateVisible) {
                wifissid = 2;
            } else if (sMobileVisible) {
                wifissid = 4;
            } else {
                wifissid = 5;
            }
            sLastWiFiTargetSummary = buildTargetSummary();
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
    struct NetworkTry {
        const char* ssidName;
        const char* pwd;
        bool shouldTry;
    };
    NetworkTry networks[] = {
        {ssid, password, sPrimaryVisible},
        {alternatessid, alternatepassword, sAlternateVisible},
        {mobilessid, mobilepassword, sMobileVisible}
    };

    auto finalizeFailureCycle = [&]() {
        if (!sPrimaryVisible && !sAlternateVisible && !sMobileVisible) {
            sLastWifiFailureReason = "target SSIDs not found in scan";
        } else if (sLastAttemptFailure.length() > 0) {
            sLastWifiFailureReason = sLastAttemptFailure;
        } else {
            sLastWifiFailureReason = "all networks failed to associate";
        }
        reportWiFiStatus("WiFi cycle failed: " + sLastWifiFailureReason);
        sWifiBackoffMs = min(kWifiMaxBackoffMs, sWifiBackoffMs * 2);
        sLastWifiAttemptMs = millis();
        sWifiAttemptInProgress = false;
        sActiveNetworkIndex = -1;
        sNextNetworkIndex = 0;
        sLastAttemptStatusReportMs = 0;
    };

    auto startAttempt = [&](int idx) {
        sActiveNetworkIndex = idx;
        sNextNetworkIndex = idx + 1;
        sWifiAttemptInProgress = true;
        sAttemptWindowStartMs = millis();
        sLastAttemptStatusReportMs = 0;
        reportWiFiStatus("Try SSID: \"" + String(networks[idx].ssidName) + "\"  backoff(ms): " + String(sWifiBackoffMs));
        WiFi.begin(networks[idx].ssidName, networks[idx].pwd);
    };

    if (WiFi.status() == WL_CONNECTED) {
        sLastWifiFailureReason = "none";
        sWifiBackoffMs = kWifiInitialBackoffMs;
        sWifiAttemptInProgress = false;
        sActiveNetworkIndex = -1;
        sNextNetworkIndex = 0;
        sLastAttemptStatusReportMs = 0;
        return true;
    }

    unsigned long nowMs = millis();
    int currentStatus = WiFi.status();

    // Force clean reconnect when link drops unexpectedly.
    if ((currentStatus == WL_CONNECTION_LOST || currentStatus == WL_CONNECT_FAILED) && !sWifiAttemptInProgress) {
        WiFi.disconnect();
    }

    if (sWifiAttemptInProgress && sActiveNetworkIndex >= 0 && sActiveNetworkIndex < 3) {
        int wifiStatus = WiFi.status();
        if (sLastAttemptStatusReportMs == 0 || (nowMs - sLastAttemptStatusReportMs) >= 1000) {
            reportWiFiStatus("SSID \"" + String(networks[sActiveNetworkIndex].ssidName) + "\" status: " + wifiStatusToString(wifiStatus));
            sLastAttemptStatusReportMs = nowMs;
        }

        if (wifiStatus == WL_CONNECTED) {
            reportWiFiStatus("WiFi connected to \"" + String(networks[sActiveNetworkIndex].ssidName) + "\"");
            reportWiFiStatus("IP: " + WiFi.localIP().toString());
            sLastWifiFailureReason = "none";
            sWifiBackoffMs = kWifiInitialBackoffMs;
            sLastWiFiTargetSummary = String(networks[sActiveNetworkIndex].ssidName);
            sWifiAttemptInProgress = false;
            sActiveNetworkIndex = -1;
            sNextNetworkIndex = 0;
            sLastAttemptStatusReportMs = 0;
            return true;
        }

        if ((nowMs - sAttemptWindowStartMs) < kWifiConnectWindowMs) {
            return false;
        }

        sLastAttemptFailure = "SSID \"" + String(networks[sActiveNetworkIndex].ssidName) + "\": " +
                              wifiStatusToString(wifiStatus) + " (" + wifiStatusHint(wifiStatus) + ")";
        reportWiFiStatus("Failed: " + sLastAttemptFailure);
        WiFi.disconnect();
        sWifiAttemptInProgress = false;
        sActiveNetworkIndex = -1;
        sLastAttemptStatusReportMs = 0;
    }

    if (sNextNetworkIndex == 0 && (nowMs - sLastWifiAttemptMs) < sWifiBackoffMs) {
        unsigned long remainingMs = sWifiBackoffMs - (nowMs - sLastWifiAttemptMs);
        sLastWifiFailureReason = "backoff wait " + String((remainingMs + 999) / 1000) + "s";
        if (sLastBackoffNoticeMs == 0 || nowMs - sLastBackoffNoticeMs >= 1000) {
            reportWiFiStatus("WiFi retry paused: " + sLastWifiFailureReason);
            sLastBackoffNoticeMs = nowMs;
        }
        return false;
    }
    sLastBackoffNoticeMs = 0;

    bool missingTargets = !sPrimaryVisible && !sAlternateVisible && !sMobileVisible;
    bool scanExpired = (sLastScanMs == 0 || (nowMs - sLastScanMs) >= kWifiRescanIntervalMs);
    if (sNextNetworkIndex == 0 && ((wifissid != 1 && wifissid != 2 && wifissid != 3 && wifissid != 4) || missingTargets || scanExpired)) {
        DynamicJsonDocument scanData(4096);
        scanNetworks(&scanData);
    }

    sLastWiFiTargetSummary = buildTargetSummary();
    if (sNextNetworkIndex == 0) {
        reportWiFiStatus("WiFi scan summary: " + sLastWiFiTargetSummary);
        sLastAttemptFailure = "";
    }

    for (int i = sNextNetworkIndex; i < 3; i++) {
        if (!networks[i].shouldTry) {
            reportWiFiStatus("Skip SSID: \"" + String(networks[i].ssidName) + "\" (not visible in scan)");
            sNextNetworkIndex = i + 1;
            continue;
        }
        startAttempt(i);
        return false;
    }

    finalizeFailureCycle();
    return false;
}

int sendPostMessage(DynamicJsonDocument* document) {
    if (WiFi.status() == WL_CONNECTED) {
        String message = "";
        serializeJson(*document, message);

        HTTPClient http;
        http.setReuse(false);
        http.setTimeout(700);
        Serial.println("Begin Connection...");
        appendEventLog("https: begin upload to " + String(SERVERURL));
        int svrResponse = http.begin(SERVERURL, root_ca);
        Serial.println("Begin response:");
        Serial.println(svrResponse);
        if (svrResponse != 1) {
            appendEventLog("https: begin failed code=" + String(svrResponse));
        }
        http.addHeader("Content-Type", "application/json; charset=utf-8");
        http.addHeader("Authorization", "Bearer " + String(BEARERTOKEN));

        Serial.println("Sending Post message...");
        int httpResponseCode = http.POST(message);
        Serial.println("Post Message Sent and received response...");

        Serial.println("Response Code: " + String(httpResponseCode));
        if (httpResponseCode == 200) {
            String response = http.getString();
            Serial.println(httpResponseCode);
            Serial.println(response);
            appendEventLog("https: upload success code=" + String(httpResponseCode));
        } else {
            Serial.print("Error on sending Information to Server: ");
            Serial.println(httpResponseCode);
            appendEventLog("https: upload failed code=" + String(httpResponseCode));
            http.end();
            return httpResponseCode;
        }
        http.end();
        return 200;
    } else {
        Serial.println("Error in WiFi connection");
        appendEventLog("https: upload skipped, wifi disconnected");
        return -1;
    }
}
