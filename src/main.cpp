#include <Arduino.h>
#include <rpcWiFi.h>
#include <millisDelay.h>
#include "RTC_SAMD51.h"

#include <ArduinoJson.h>
#if __has_include(<Seeed_SHT35.h>)
#include <Seeed_SHT35.h>
#else
#include "../.pio/libdeps/seeed_wio_terminal/Grove - I2C High Accuracy Temp_Humi Sensor SHT35/Seeed_SHT35.h"
#endif
#if __has_include("secrets_local.h")
#include "secrets_local.h"
#else
#include "secrets_template.h"
#endif
#include "SensirionI2cSht4x.h"
#include <Wire.h>
#include "time.h"

#include "TFT_eSPI.h"
#include "Free_Fonts.h"
#include <SPI.h>
#include <Seeed_FS.h> //Including SD card library
#include "SD/Seeed_SD.h"
#include "RawImage.h"  //Including image processing library
#include <HTTPClient.h>
#include <rpcWiFiClientSecure.h>
#include "network.h"
#include "display.h"
#include "sensors.h"
#include "storage.h"
#include <ctype.h>

#include <rpcBLEDevice.h>
// #include <BLEServer.h>
#include <erpc/erpc_port.h>

#define SERVICER_UUID        "87654320-1234-1234-1234-123456789abc"//"180f"
#define SERVICEW_UUID        "12345678-2345-6789-1234-56789abcdef0"//"180f"
#define TEMPERATURE_UUID    "87654321-4321-8765-4321-abcdef012345"
#define HUMIDITY_UUID       "87654322-4321-8765-4321-abcdef012346"
#define PLACE_UUID          "12345677-1234-5678-1234-56789abcdef1" //"87654321-4321-8765-4321-abcdef012344"
#define DESCRIPTOR_UUID     "12345677-4322-8766-4322-abcdef678910"

BLEServer *pServer = NULL;
BLEService *globalServicePtrR = nullptr;
BLEService *globalServicePtrW = nullptr;
BLEDescriptor *pDescriptor = NULL;
BLECharacteristic * temperatureCharacteristic;
BLECharacteristic * humidityCharacteristic;
BLECharacteristic * placeCharacteristic;

// Screen Definitions
TFT_eSPI tft;
TFT_eSprite spr = TFT_eSprite(&tft); //Initializing buffer
#define LCD_BACKLIGHT (72Ul) // Control Pin of LCD

#define TFT_PROCOMSABLUE 0x2A51   /*  0x2A51  43.0,  74.0,  142.0  RGB565  */
int bscreenOn = false;


// Wireless Definitions

#define HOSTNAME WIFI_HOSTNAME
#define SERVERURL API_SERVER_URL // https://thermolog.gonmobile.com/api/reading
// #define ALTERNATESVR "https://ace.rea.local/api/reading"
// #define SVRPATH "/api/reading"
#define K 27.55 // Constant defined for meters and MegaHertz
#define BEARERTOKEN API_BEARER_TOKEN
String strBaseMac = "";  // WiFi MacAddress
const char* ssid     = WIFI_SSID_PRIMARY; // "FGranGonz5G";
const char* password = WIFI_PASSWORD_PRIMARY; // "Aqu1l3sBa3sa157";
const char* alternatessid     = WIFI_SSID_ALTERNATE; //EmpleadosProcomsa
const char* alternatepassword = WIFI_PASSWORD_ALTERNATE; //Pr0c0msaEmpl3ados
const char* mobilessid     = WIFI_SSID_MOBILE; //EmpleadosProcomsa
const char* mobilepassword = WIFI_PASSWORD_MOBILE; //Pr0c0msaEmpl3ados
int wifissid = 0; // 0=Unknown, 1=Primary, 2=Alternate, 3=Both, 4=NotFound
int channels[11] = { 2412, 2417, 2422, 2427, 2432, 2437, 2442, 2447, 2452, 2457, 2462 };
float distance; // In meters
WiFiClientSecure client;

// Timecontrol Definitions
unsigned long currentMillis = millis(); 
unsigned long previousMillis = 0;
unsigned long previousMillisC= millis();
unsigned long screenMillis = 0;
unsigned long lastRefreshMillis = 0;
unsigned long wioKEYBMillis = 0;
unsigned long wioKEYCMillis = 0;

const unsigned long interval = 60000; // interval between scans 60 seconds
const unsigned long screenInterval = 10000; // interval to keep screen display 10 seconds
const unsigned long screenRefreshInterval = 1000; // redraw cadence while screen is active
const unsigned long seconds = interval/1000;


// Sensirion Definitions

// macro definitions
// make sure that we use the proper definition of NO_ERROR
#ifdef NO_ERROR
#undef NO_ERROR
#endif
#define NO_ERROR 0

SensirionI2cSht4x sensor;
SHT35 sensor35(PIN_WIRE_SCL, DEFAULT_IIC_ADDR);

char errorMessage[64];
int16_t error;
uint32_t serialNumber;
float aTemperature = 0.00;
float aHumidity = 0.00;
bool termosensor = false;
bool termosensor35 = false;

// Light Sensor
  int light;

// NTP server to request epoch time
const char* ntpServer = "mx.pool.ntp.org"; //mx.pool.ntp.org
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.windows.com";
const char* ntpServer3 = "time.cloudflare.com";

millisDelay updateDelay; // the update delay object. used for ntp periodic update.

unsigned int localPort = 2390;      // local port to listen for UDP packets
char timeServer[] = "mx.pool.ntp.org"; // extenral NTP server e.g. time.nist.gov
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
DateTime now; // declare a time object
WiFiUDP udp; //The udp library class
unsigned long devicetime; // localtime
RTC_SAMD51 rtc;
String tnow = "";

// Variable to save current epoch time
// unsigned long epochTime; 
// struct tm tCurrInfo;
bool timeSet = false;

// Variable for LogFile
File LogFile;
File screenLogo;
File iconLogo;
bool sdcard = false;
bool fwritten = false;
bool syslogCreated = false;
bool unsentlogCreated = false;
unsigned long bleRenameUnlockUntilMs = 0;
unsigned long bleRenameUnlockWindowMs = 120000;


// RootCA Certificate
const char* root_ca= \
"-----BEGIN CERTIFICATE-----\n" \
"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\n" \
"-----END CERTIFICATE-----\n";

// You can use x.509 client certificates if you want
//const char* test_client_key = "";   //to verify the client
//const char* test_client_cert = "";  //to verify the client


// CODE START

// Bluetooth Async Call
    // Custom security callbacks
// class MySecurityCallbacks: public BLESecurityCallbacks {
//   uint32_t onPassKeyRequest() override {
//     Serial.println("Passkey requested");
//     return 123456; // Static passkey (can be randomized)
//   }

//   void onPassKeyNotify(uint32_t passkey) override {
//     Serial.print("Passkey Notify: ");
//     Serial.println(passkey);
//   }

//   bool onConfirmPIN(uint32_t passkey) override {
//     Serial.print("Confirm Passkey: ");
//     Serial.println(passkey);
//     return true; // Accept the passkey
//   }

//   bool onSecurityRequest() override {
//     Serial.println("Security request received");
//     return true; // Allow security requests
//   }

//   void onAuthenticationComplete(esp_ble_auth_cmpl_t auth_cmpl) override {
//     if (auth_cmpl.success) {
//       Serial.println("Authentication successful!");
//     } else {
//       Serial.println("Authentication failed.");
//     }
//   }
// };

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
    //   deviceConnected = true;
    //   spr.fillSprite(TFT_BLACK);
    //   spr.createSprite(240, 100);
    //   spr.setTextColor(TFT_WHITE, TFT_BLACK);
    //   spr.setFreeFont(&FreeSansBoldOblique12pt7b);
    //   spr.drawString("Message: ", 20, 70);
    //   spr.setTextColor(TFT_GREEN, TFT_BLACK);
    //   spr.drawString("status: connected",10 ,5); 
    //   spr.pushSprite(0, 0);
        Serial.println("devConnected");
    };
 
    void onDisconnect(BLEServer* pServer) {
    //   deviceConnected = false;
    //   Serial.print("123123");
    //   spr.fillSprite(TFT_BLACK);
    //   spr.createSprite(240, 100);
    //   spr.setTextColor(TFT_WHITE, TFT_BLACK);
    //   spr.setFreeFont(&FreeSansBoldOblique12pt7b);
    //   spr.drawString("Message: ", 20, 70);
    //   spr.setTextColor(TFT_RED, TFT_BLACK);
    //   spr.drawString("status: disconnect",10 ,5); 
    //   spr.pushSprite(0, 0);
        Serial.println("devDisconnected");
    }
};

static const unsigned long kBleRenameUnlockDebounceMs = 1000;

static const unsigned long kBleUnlockPresetMs[] = {30000UL, 60000UL, 120000UL};

static void cycleBleUnlockWindow(bool increase) {
    size_t selectedIndex = 0;
    for (size_t i = 0; i < (sizeof(kBleUnlockPresetMs) / sizeof(kBleUnlockPresetMs[0])); ++i) {
        if (bleRenameUnlockWindowMs == kBleUnlockPresetMs[i]) {
            selectedIndex = i;
            break;
        }
    }

    if (increase) {
        selectedIndex = (selectedIndex + 1) % (sizeof(kBleUnlockPresetMs) / sizeof(kBleUnlockPresetMs[0]));
    } else {
        selectedIndex = (selectedIndex + (sizeof(kBleUnlockPresetMs) / sizeof(kBleUnlockPresetMs[0])) - 1) %
                        (sizeof(kBleUnlockPresetMs) / sizeof(kBleUnlockPresetMs[0]));
    }

    bleRenameUnlockWindowMs = kBleUnlockPresetMs[selectedIndex];
    appendEventLog("ble: unlock window set to " + String(bleRenameUnlockWindowMs / 1000UL) + "s");
}

static bool isBleRenameWriteUnlocked() {
    return millis() < bleRenameUnlockUntilMs;
}

static String sanitizePlaceValue(const std::string& rawValue) {
    const size_t kMaxPlaceLen = 20;
    String cleaned = "";
    cleaned.reserve(kMaxPlaceLen);

    for (size_t i = 0; i < rawValue.length() && cleaned.length() < kMaxPlaceLen; ++i) {
        unsigned char ch = static_cast<unsigned char>(rawValue[i]);
        if (isalnum(ch) || ch == ' ' || ch == '_' || ch == '-') {
            cleaned += static_cast<char>(ch);
        }
    }

    cleaned.trim();
    if (cleaned.length() == 0) {
        return "Anywhere";
    }
    return cleaned;
}

class PlaceCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        Serial.println("onWrite triggered");
        if (!isBleRenameWriteUnlocked()) {
            appendEventLog("ble: rename rejected (locked)");
            Serial.println("Rejected place update: BLE rename lock is active");
            return;
        }
        std::string placeValue = pCharacteristic->getValue();
        String safeValue = sanitizePlaceValue(placeValue);
        pCharacteristic->setValue(safeValue.c_str());
        appendEventLog("ble: place updated=" + safeValue);
        Serial.print("Accepted place: ");
        Serial.println(safeValue);
    }
    // void onRead(BLECharacteristic *pCharacteristic){
    //     Serial.println("aPlace");
    // }
};

class TemperatureCallbacks: public BLECharacteristicCallbacks {
    // void onWrite(BLECharacteristic *pCharacteristic) {
    //   std::string rxValue = pCharacteristic->getValue();

    //   if (rxValue.length() > 0) {
    //     Serial.println("*********");
    //     Serial.print("Received Value: ");
    //     for (int i = 0; i < rxValue.length(); i++)
    //       Serial.print(rxValue[i]);

    //     Serial.println();
    //     Serial.println("*********");
    //   }
    // }
    void onRead(BLECharacteristic *pCharacteristic){
        Serial.println("aHumidity");
    }
};

class HumidityCallbacks: public BLECharacteristicCallbacks {
    // void onWrite(BLECharacteristic *pCharacteristic) {
    //   std::string rxValue = pCharacteristic->getValue();

    //   if (rxValue.length() > 0) {
    //     Serial.println("*********");
    //     Serial.print("Received Value: ");
    //     for (int i = 0; i < rxValue.length(); i++)
    //       Serial.print(rxValue[i]);

    //     Serial.println();
    //     Serial.println("*********");
    //   }
    // }
    void onRead(BLECharacteristic *pCharacteristic){
        Serial.println("aHumidity");
    }
};


enum UiWindow {
    UI_WINDOW_MAIN = 0,
    UI_WINDOW_MENU,
    UI_WINDOW_LOG_MENU,
    UI_WINDOW_LOG_VIEW,
    UI_WINDOW_CONFIG
};

static UiWindow sUiWindow = UI_WINDOW_MAIN;
static int sMenuSelection = 0;
static int sLogMenuSelection = 0;
static int sLogScrollOffset = 0;
static bool sShowEventsLog = false;
static bool sJoystickLocked = false;
static const int kLogPageSize = 10;
static unsigned long sLastNtpSyncMs = 0;
static const unsigned long kNtpResyncIntervalMs = 6UL * 60UL * 60UL * 1000UL;
static unsigned long sLastPostProcessMs = 0;
static const unsigned long kPostProcessIntervalMs = 15000;

static bool isUserInteracting() {
    return digitalRead(WIO_KEY_A) == LOW ||
           digitalRead(WIO_KEY_B) == LOW ||
           digitalRead(WIO_5S_UP) == LOW ||
           digitalRead(WIO_5S_DOWN) == LOW ||
           digitalRead(WIO_5S_LEFT) == LOW ||
           digitalRead(WIO_5S_RIGHT) == LOW ||
           digitalRead(WIO_5S_PRESS) == LOW;
}

static bool isJoystickPressed(int pin) {
    return digitalRead(pin) == LOW;
}

static bool isAnyJoystickDirectionPressed() {
    return isJoystickPressed(WIO_5S_UP) ||
           isJoystickPressed(WIO_5S_DOWN) ||
           isJoystickPressed(WIO_5S_LEFT) ||
           isJoystickPressed(WIO_5S_RIGHT) ||
           isJoystickPressed(WIO_5S_PRESS);
}

static void renderActiveWindow() {
    if (sUiWindow == UI_WINDOW_MAIN) {
        sendToScreen();
    } else if (sUiWindow == UI_WINDOW_MENU) {
        sendMenuScreen(sMenuSelection);
    } else if (sUiWindow == UI_WINDOW_LOG_MENU) {
        sendLogMenuScreen(sLogMenuSelection);
    } else if (sUiWindow == UI_WINDOW_LOG_VIEW) {
        sendLogViewerScreen(sShowEventsLog, sLogScrollOffset);
    } else {
        sendConfigScreen();
    }
}

static void handleJoystickNavigation() {
    if (sJoystickLocked) {
        if (!isAnyJoystickDirectionPressed()) {
            sJoystickLocked = false;
        }
        return;
    }

    if (isJoystickPressed(WIO_5S_RIGHT)) {
        if (sUiWindow == UI_WINDOW_MAIN) {
            sUiWindow = UI_WINDOW_MENU;
            renderActiveWindow();
        }
        sJoystickLocked = true;
        return;
    }

    if (isJoystickPressed(WIO_5S_LEFT)) {
        if (sUiWindow == UI_WINDOW_MENU) {
            sUiWindow = UI_WINDOW_MAIN;
            renderActiveWindow();
        } else if (sUiWindow == UI_WINDOW_LOG_MENU || sUiWindow == UI_WINDOW_CONFIG) {
            sUiWindow = UI_WINDOW_MENU;
            renderActiveWindow();
        } else if (sUiWindow == UI_WINDOW_LOG_VIEW) {
            sUiWindow = UI_WINDOW_LOG_MENU;
            renderActiveWindow();
        }
        sJoystickLocked = true;
        return;
    }

    if (sUiWindow == UI_WINDOW_MENU && isJoystickPressed(WIO_5S_UP)) {
        sMenuSelection = (sMenuSelection + 2) % 3;
        renderActiveWindow();
        sJoystickLocked = true;
        return;
    }

    if (sUiWindow == UI_WINDOW_MENU && isJoystickPressed(WIO_5S_DOWN)) {
        sMenuSelection = (sMenuSelection + 1) % 3;
        renderActiveWindow();
        sJoystickLocked = true;
        return;
    }

    if (sUiWindow == UI_WINDOW_MENU && isJoystickPressed(WIO_5S_PRESS)) {
        if (sMenuSelection == 0) {
            sUiWindow = UI_WINDOW_LOG_MENU;
        } else if (sMenuSelection == 1) {
            sUiWindow = UI_WINDOW_MAIN;
        } else {
            sUiWindow = UI_WINDOW_CONFIG;
        }
        renderActiveWindow();
        sJoystickLocked = true;
        return;
    }

    if (sUiWindow == UI_WINDOW_LOG_MENU && isJoystickPressed(WIO_5S_UP)) {
        sLogMenuSelection = (sLogMenuSelection + 1) % 2;
        renderActiveWindow();
        sJoystickLocked = true;
        return;
    }

    if (sUiWindow == UI_WINDOW_LOG_MENU && isJoystickPressed(WIO_5S_DOWN)) {
        sLogMenuSelection = (sLogMenuSelection + 1) % 2;
        renderActiveWindow();
        sJoystickLocked = true;
        return;
    }

    if (sUiWindow == UI_WINDOW_LOG_MENU && isJoystickPressed(WIO_5S_PRESS)) {
        sShowEventsLog = (sLogMenuSelection == 1);
        sLogScrollOffset = 0;
        sUiWindow = UI_WINDOW_LOG_VIEW;
        renderActiveWindow();
        sJoystickLocked = true;
        return;
    }

    if (sUiWindow == UI_WINDOW_LOG_VIEW && isJoystickPressed(WIO_5S_UP)) {
        sLogScrollOffset = max(0, sLogScrollOffset - kLogPageSize);
        renderActiveWindow();
        sJoystickLocked = true;
        return;
    }

    if (sUiWindow == UI_WINDOW_LOG_VIEW && isJoystickPressed(WIO_5S_DOWN)) {
        sLogScrollOffset += kLogPageSize;
        renderActiveWindow();
        sJoystickLocked = true;
        return;
    }

    if (sUiWindow == UI_WINDOW_CONFIG && isJoystickPressed(WIO_5S_UP)) {
        cycleBleUnlockWindow(false);
        renderActiveWindow();
        sJoystickLocked = true;
        return;
    }

    if (sUiWindow == UI_WINDOW_CONFIG && isJoystickPressed(WIO_5S_DOWN)) {
        cycleBleUnlockWindow(true);
        renderActiveWindow();
        sJoystickLocked = true;
        return;
    }

    if (sUiWindow == UI_WINDOW_CONFIG && isJoystickPressed(WIO_5S_PRESS)) {
        setAckValidationEnabled(!isAckValidationEnabled());
        renderActiveWindow();
        sJoystickLocked = true;
    }
}


// End Bluetooth

static void logVisibleNetworksToStartupMonitor() {
    DynamicJsonDocument scanData(4096);
    wifissid = 0;
    scanNetworks(&scanData);

    JsonObject aps = scanData.as<JsonObject>();
    int shownCount = 0;
    for (JsonPair kv : aps) {
        JsonObject ap = kv.value().as<JsonObject>();
        String apName = ap["name"] | "";
        int signal = ap["signal"] | 0;
        if (apName.length() == 0) {
            continue;
        }

        String line = apName + " (" + String(signal) + "dBm)";
        logStartupStatus(line);
        shownCount++;
        if (shownCount >= 6) {
            break;
        }
    }

    if (shownCount == 0) {
        logStartupStatus("No APs visible");
    }
}

static void waitForWifiDebugOrContinue() {
    unsigned long lastRefreshMs = 0;
    while (WiFi.status() != WL_CONNECTED) {
        if (digitalRead(WIO_KEY_A) == LOW || digitalRead(WIO_KEY_B) == LOW) {
            break;
        }

        if (lastRefreshMs == 0 || millis() - lastRefreshMs >= 4000) {
            logStartupStatus("WiFi retry + scan...");
            if (ensureWiFiConnected()) {
                logStartupStatus("Connected: " + WiFi.SSID());
                logStartupStatus("IP: " + WiFi.localIP().toString());
                break;
            }

            logStartupStatus(getLastWiFiFailureReason());
            logStartupStatus(getLastWiFiTargetSummary());
            logVisibleNetworksToStartupMonitor();
            logStartupStatus("WiFi status: " + wifiStatusToString(WiFi.status()));
            logStartupStatus("Press A or B to continue");
            lastRefreshMs = millis();
        }

        delay(100);
    }

    delay(150);
}

static void startupWiFiStatusSink(const String& message) {
    logStartupStatus(message);
}


void setup() {
    Serial.begin(9600);

    tft.begin();
    tft.setRotation(3);
    bool spriteReady = false;
    spr.setColorDepth(16);
    spriteReady = (spr.createSprite(320, 240) != nullptr);
    if (!spriteReady) {
        // Fall back to 8-bit to reduce RAM pressure on Wio Terminal.
        spr.setColorDepth(8);
        spriteReady = (spr.createSprite(320, 240) != nullptr);
    }
    beginStartupStatus();
    logStartupStatus(spriteReady ? "Display buffer ready" : "Display buffer alloc failed");
    logStartupStatus("Serial and display initialized");

    pinMode(WIO_LIGHT, INPUT);
    pinMode(WIO_BUZZER, OUTPUT);
    logStartupStatus("I/O pins configured");

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.disconnect();
    setWiFiStatusCallback(startupWiFiStatusSink);
    logStartupStatus("WiFi stack initialized");

    Serial.println("Initializing SDCard...");
    if (!SD.begin(SDCARD_SS_PIN, SDCARD_SPI)) {
        Serial.println("Initialization failed!");
        logStartupStatus("SD init failed");
    } else {
        Serial.println("File initialization done.");
        logStartupStatus("SD init ok");
        sdcard = true;
        syslogCreated = SD.exists("readings" + String(serialNumber) + ".log");
        unsentlogCreated = SD.exists("unsent" + String(serialNumber) + ".log");
    }

    Wire.begin();
    termosensor = false;
    logStartupStatus("SHT40 disabled (temporary)");

    if (sensor35.init() == NO_ERROR) {
        logStartupStatus("SHT35 init ok");
        // Retry up to 5 times — sensor may need time after Wire.begin()
        for (int attempt = 1; attempt <= 5; attempt++) {
            delay(100);
            error = sensor35.read_meas_data_single_shot(HIGH_REP_WITH_STRCH, &aTemperature, &aHumidity);
            Serial.print("SHT35 read attempt " + String(attempt) + " error=");
            Serial.println(error);
            if (error == NO_ERROR) {
                termosensor35 = true;
                logStartupStatus("SHT35 ready (attempt " + String(attempt) + ")");
                break;
            }
        }
        if (!termosensor35) {
            logStartupStatus("SHT35 read failed after 5 attempts");
            Serial.print("SHT35 last error code: ");
            Serial.println(error);
        }
    } else {
        termosensor35 = false;
        logStartupStatus("SHT35 init failed");
        Serial.println("SHT35 init error");
    }

    if (!rtc.begin()) {
        logStartupStatus("RTC not found (continue)");
    } else {
        now = rtc.now();
        logStartupStatus("RTC available");
    }

    pinMode(WIO_KEY_A, INPUT_PULLUP);
    pinMode(WIO_KEY_B, INPUT_PULLUP);
    pinMode(WIO_KEY_C, INPUT_PULLUP);
    pinMode(WIO_5S_UP, INPUT_PULLUP);
    pinMode(WIO_5S_DOWN, INPUT_PULLUP);
    pinMode(WIO_5S_LEFT, INPUT_PULLUP);
    pinMode(WIO_5S_RIGHT, INPUT_PULLUP);
    pinMode(WIO_5S_PRESS, INPUT_PULLUP);
    logStartupStatus("Buttons configured");

    client.setCACert(root_ca);
    logStartupStatus("TLS certificate loaded");

    Serial.println("Starting BLE work!");
    BLEDevice::init("GGGTermoViewer");

    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    globalServicePtrW = pServer->createService(SERVICEW_UUID);
    placeCharacteristic = globalServicePtrW->createCharacteristic(
        PLACE_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_READ
    );
    // Keep permissions compatible with existing mobile app; writes are gated by local unlock window.
    placeCharacteristic->setAccessPermissions(GATT_PERM_READ | GATT_PERM_WRITE);
    pDescriptor = placeCharacteristic->createDescriptor(
        DESCRIPTOR_UUID,
        ATTRIB_FLAG_ASCII_Z,
        GATT_PERM_READ | GATT_PERM_WRITE,
        21
    );
    pDescriptor->setValue("Place in House");
    placeCharacteristic->setCallbacks(new PlaceCallbacks());
    placeCharacteristic->setValue("Anywhere");
    globalServicePtrW->start();

    globalServicePtrR = pServer->createService(SERVICER_UUID);
    temperatureCharacteristic = globalServicePtrR->createCharacteristic(
        TEMPERATURE_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    temperatureCharacteristic->setAccessPermissions(GATT_PERM_READ);
    temperatureCharacteristic->setValue("Temperature");
    temperatureCharacteristic->setCallbacks(new TemperatureCallbacks());

    humidityCharacteristic = globalServicePtrR->createCharacteristic(
        HUMIDITY_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    humidityCharacteristic->setAccessPermissions(GATT_PERM_READ);
    humidityCharacteristic->setValue("Humidity");
    humidityCharacteristic->setCallbacks(new HumidityCallbacks());

    globalServicePtrR->start();
    logStartupStatus("BLE services started");

    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICEW_UUID);
    pAdvertising->addServiceUUID(SERVICER_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
    logStartupStatus("BLE advertising active");

    logStartupStatus("Scanning WiFi networks...");
    logVisibleNetworksToStartupMonitor();

    if (ensureWiFiConnected()) {
        logStartupStatus("Connected: " + WiFi.SSID());
        logStartupStatus("IP: " + WiFi.localIP().toString());
        if (syncRtcFromNtp()) {
            sLastNtpSyncMs = millis();
            logStartupStatus("NTP sync ok");
        } else {
            logStartupStatus("NTP sync failed");
        }
    } else {
        logStartupStatus("WiFi connect failed");
        logStartupStatus(getLastWiFiFailureReason());
        logStartupStatus(getLastWiFiTargetSummary());
        logStartupStatus("Looking for: " + String(ssid));
        waitForWifiDebugOrContinue();
        if (WiFi.status() != WL_CONNECTED) {
            logStartupStatus("Continuing without WiFi");
        } else {
            if (syncRtcFromNtp()) {
                sLastNtpSyncMs = millis();
                logStartupStatus("NTP sync ok");
            } else {
                logStartupStatus("NTP sync failed");
            }
        }
    }

    previousMillis = currentMillis - interval;

    DynamicJsonDocument bootEnvData(4096);
    if (termosensor35) {
        getEnvironmentData(&bootEnvData, 35);
    }
    light = analogRead(WIO_LIGHT);

    endStartupStatus();
    setWiFiStatusCallback(nullptr);
    {
        unsigned long waitStart = millis();
        tft.setTextFont(2);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString("Press A/B to continue (3s)", 6, 222);
        while (digitalRead(WIO_KEY_A) != LOW && digitalRead(WIO_KEY_B) != LOW) {
            if ((millis() - waitStart) >= 3000) {
                break;
            }
            delay(50);
        }
        delay(150);
    }
        renderActiveWindow();
    screenMillis = millis();
    lastRefreshMillis = screenMillis;
}
void loop() {
  // put your main code here, to run repeatedly:
    currentMillis = millis();
        handleJoystickNavigation();

    if (digitalRead(WIO_KEY_C) == LOW && (currentMillis - wioKEYCMillis) > kBleRenameUnlockDebounceMs) {
        wioKEYCMillis = currentMillis;
        bleRenameUnlockUntilMs = currentMillis + bleRenameUnlockWindowMs;
        appendEventLog("ble: rename unlocked via C for " + String(bleRenameUnlockWindowMs / 1000UL) + "s");
        if (sUiWindow == UI_WINDOW_CONFIG) {
            renderActiveWindow();
        }
    }

    if (WiFi.status() != WL_CONNECTED) {
        // Keep reconnection attempts independent from display refresh cadence.
        ensureWiFiConnected();
    }

    if (digitalRead(WIO_KEY_A) == LOW) {
        screenMillis = currentMillis;
        bscreenOn = false;
        Serial.println("screenmillis:" + String(screenMillis));
    } else if (digitalRead(WIO_KEY_B) == LOW && (currentMillis - wioKEYBMillis > 4000)) {
        wioKEYBMillis = currentMillis;
        bscreenOn = !bscreenOn;
        Serial.println("bscreenOn:" + String(bscreenOn));
    }


    if (currentMillis - previousMillis >= interval) {

        previousMillis = currentMillis; // update the last blink time
        
        // Get Environment Data
        // If SHT35 failed in setup, try to recover it once per cycle
        if (!termosensor35) {
            Serial.println("SHT35 not active — attempting recovery...");
            if (sensor35.init() == NO_ERROR) {
                error = sensor35.read_meas_data_single_shot(HIGH_REP_WITH_STRCH, &aTemperature, &aHumidity);
                Serial.print("SHT35 recovery read error="); Serial.println(error);
                if (error == NO_ERROR) {
                    termosensor35 = true;
                    Serial.println("SHT35 recovered ok");
                }
            }
        }
        DynamicJsonDocument envData(4096);
        if (termosensor){
            getEnvironmentData(&envData, 40);
        } 
        if(termosensor35){
            getEnvironmentData(&envData, 35);
        }
        light = analogRead(WIO_LIGHT);
        Serial.println("Luz:" + String(light));

                // Always queue first on disk, then background sender confirms with HTTP 200.
                if (!enqueuePostForRetry(&envData)) {
                        appendEventLog("queue: enqueue failed");
                }
                if (WiFi.status() != WL_CONNECTED) {
                    Serial.println("WiFi not connected, payload queued for retry.");
        }
        writeDataLogFile(&envData, false);

        Serial.println(String(placeCharacteristic->getValue().c_str()));

        uint8_t* descValue = pDescriptor->getValue();
        if (descValue != nullptr) {
            Serial.print("Current Descriptor Value: ");
            for (int i = 0; i < 21; i++) { // Replace 2 with the actual length of your descriptor
                Serial.print((char)descValue[i]); // Convert and print each byte as a character
            }
            Serial.println();
        }
    }

        bool screenShouldBeOn = bscreenOn || (currentMillis - screenMillis <= screenInterval);
        if (screenShouldBeOn) {
            digitalWrite(LCD_BACKLIGHT, HIGH);
            if (currentMillis - lastRefreshMillis >= screenRefreshInterval) {
                renderActiveWindow();
                lastRefreshMillis = currentMillis;
            }
        } else if (digitalRead(LCD_BACKLIGHT) == HIGH) {
            digitalWrite(LCD_BACKLIGHT, LOW);
    }

        if (WiFi.status() == WL_CONNECTED &&
                (sLastNtpSyncMs == 0 || (currentMillis - sLastNtpSyncMs) >= kNtpResyncIntervalMs)) {
                if (syncRtcFromNtp()) {
                        sLastNtpSyncMs = currentMillis;
                }
        }

        if (WiFi.status() == WL_CONNECTED &&
            (sLastPostProcessMs == 0 || (currentMillis - sLastPostProcessMs) >= kPostProcessIntervalMs)) {
            // Only process queued posts when user is idle to avoid input lag.
            if (!isUserInteracting() && sUiWindow == UI_WINDOW_MAIN) {
                processPendingPosts(1);
                sLastPostProcessMs = currentMillis;
            }
        }


  // Write File
    // if (!fwritten){
    //   unSentLog = SD.open("unsent.txt", FILE_WRITE); //Writing Mode
    //   // if the file opened okay, write to it:
    //   if (unSentLog) {
    //     Serial.print("Writing to test.txt...");
    //     unSentLog.println("testing 1, 2, 3."); //Writing this to the txt file
    //     // close the file:
    //     unSentLog.close();
    //     Serial.println("done.");
    //     fwritten = true;
    //   } else {
    //     // if the file didn't open, print an error:
    //     Serial.println("error opening test.txt");
    //   }
    // }


  // Read File
  // myFile = SD.open("test.txt", FILE_READ); //Read Mode
  //   if (myFile) {
  //     Serial.println("test.txt:");

  //     // read from the file until there's nothing else in it:
  //     while (myFile.available()) {
  //       Serial.write(myFile.read());
  //     }
  //     // close the file:
  //     myFile.close();
  //   } else {
  //     // if the file didn't open, print an error:
  //     Serial.println("error opening test.txt");
  //   }
  // }  


}
