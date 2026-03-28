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

const unsigned long interval = 60000; // interval between scans 60 seconds
const unsigned long screenInterval = 10000; // interval to keep screen display 10 seconds
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

class PlaceCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        Serial.println("onWrite triggered");
        std::string placeValue = pCharacteristic->getValue();
        Serial.print("Received: ");
        Serial.println(String(placeValue.c_str()));
        // pCharacteristic->setValue(placeValue);
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


// End Bluetooth


void setup() {
    Serial.begin(9600);

    tft.begin();
    tft.setRotation(3);
    spr.createSprite(TFT_HEIGHT, TFT_WIDTH); //Create buffer
    beginStartupStatus();
    logStartupStatus("Serial and display initialized");

    // Set PIN Modes
    pinMode(WIO_LIGHT, INPUT); //Set light sensor pin as INPUT
    pinMode(WIO_BUZZER, OUTPUT); //Set buzzer pin as OUTPUT
    logStartupStatus("I/O pins configured");

 
    // Set WiFi to station mode and disconnect from an AP if it was previously connected
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.disconnect();
    logStartupStatus("WiFi stack initialized");

    // SDCard Setup
    Serial.println("Initializing SDCard...");
    if (!SD.begin(SDCARD_SS_PIN, SDCARD_SPI)) {
        Serial.println("Initialization failed!");
        logStartupStatus("SD init failed");
        while(1);
    } else{
        Serial.println("File initialization done.");
        logStartupStatus("SD init ok");
        sdcard = true;
        syslogCreated = SD.exists("readings" + String(serialNumber) + ".log");
        unsentlogCreated = SD.exists("unsent" + String(serialNumber) + ".log");
        Serial.println("readingFile status:" + String(syslogCreated));
        Serial.println("unsentFile status:" + String(unsentlogCreated));
    }
    delay(2500);
    // drawImage<u_int16_t>("procomsa_logotipo_318x238.bmp", 1, 1);
    // drawImage<u_int16_t>("Procomsa_fi_whiteTxt_32x32.bmp", 15, 10);

    // Sensirion Setup
    Wire.begin();

    // SHT40 temporarily disabled due startup failures under investigation.
    termosensor = false;
    logStartupStatus("SHT40 disabled (temporary)");

    // SHT35 Sensor HT Sensor
    if (sensor35.init() == NO_ERROR){
        error = sensor35.read_meas_data_single_shot(HIGH_REP_WITH_STRCH, &aTemperature, &aHumidity);

        Serial.println("Sensor Read Result: " + String(errorMessage));

        if (error == NO_ERROR){
            termosensor35 = true;
            logStartupStatus("SHT35 read ok");
        } else {
            logStartupStatus("SHT35 read error");
        }
        Serial.println("SHT35 sensor initialization successful :" + termosensor35);
        logStartupStatus("SHT35 ready");
    } else {
        Serial.println("SHT35 sensor initialization failed");
        logStartupStatus("SHT35 init failed");
    }




    // WiFi Setup
    DynamicJsonDocument scanData(4096); //4096
    scanNetworks(&scanData);
    if (wifissid != 1 && wifissid != 2 && wifissid != 3) {
      Serial.print("No Available Wifi networks found."); 
            logStartupStatus("WiFi AP not found");

    } else {
      // char* version = rpc_system_version();
      // Serial.print("RTL8720 Firmware Version: ");
      // Serial.println(version);
      // erpc_free(version);

      connectWiFi();
      strBaseMac = WiFi.macAddress();
      Serial.print("MAC address: ");
      Serial.println(strBaseMac);
    logStartupStatus("WiFi connected");

      // get the time via NTP (udp) call to time server
      // getNTPtime returns epoch UTC time adjusted for timezone but not daylight savings
      // time
      devicetime = getNTPtime();

      // check if rtc present
      if (devicetime == 0) {
          Serial.println("Failed to get time from network time server.");
          logStartupStatus("NTP sync failed");
      }

      if (!rtc.begin()) {
          Serial.println("Couldn't find RTC");
          logStartupStatus("RTC not found");
          while (1) delay(10); // stop operating
      }

      // get and print the current rtc time
      now = rtc.now();
      Serial.print("RTC time is: ");
      Serial.println(now.timestamp(DateTime::TIMESTAMP_FULL));
    logStartupStatus("RTC sync ok");

      // adjust time using ntp time
      rtc.adjust(DateTime(devicetime));

      // print boot update details
      Serial.println("RTC (boot) time updated.");
      // get and print the adjusted rtc time
      now = rtc.now();
      Serial.print("Adjusted RTC (boot) time is: ");
      Serial.println(now.timestamp(DateTime::TIMESTAMP_FULL));

            // Boot-time recovery: drain queued unsent telemetry when possible.
            resendUnsentLogs(20);
                logStartupStatus("Unsent queue checked");

    }

    // // Usr button Setup
    // pinMode(buttonPin, INPUT_PULLUP);
    pinMode(WIO_LIGHT, INPUT);
    pinMode(WIO_KEY_A, INPUT_PULLUP);
    pinMode(WIO_KEY_B, INPUT_PULLUP);
    logStartupStatus("Buttons configured");

    client.setCACert(root_ca);
    logStartupStatus("TLS certificate loaded");
    //client.setCertificate(test_client_key); // for client verification
    //client.setPrivateKey(test_client_cert); // for client verification


    // Setting Up Bluetooth    

    Serial.println("Starting BLE work!");

    String devName = "GGG TermoViewer " + String(serialNumber);
    Serial.println(devName);
    BLEDevice::init("GGGTermoViewer"); // devName.c_str()

    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    globalServicePtrW = pServer->createService(SERVICEW_UUID);

    placeCharacteristic = globalServicePtrW->createCharacteristic(
                                            PLACE_UUID,
                                            BLECharacteristic::PROPERTY_WRITE | 
                                            BLECharacteristic::PROPERTY_READ
                                        ); //   | BLECharacteristic::PROPERTY_WRITE_NR | BLECharacteristic::PROPERTY_NOTIFY

    placeCharacteristic->setAccessPermissions(GATT_PERM_READ | GATT_PERM_WRITE); //  | GATT_PERM_NOTIF_IND
    pDescriptor = placeCharacteristic->createDescriptor(
                                         DESCRIPTOR_UUID,
                                         ATTRIB_FLAG_ASCII_Z,
                                         GATT_PERM_READ | GATT_PERM_WRITE,
                                         21
                                         ); //  ATTRIB_FLAG_VOID | 

    pDescriptor->setValue("Place in House");
    placeCharacteristic->setCallbacks(new PlaceCallbacks());
    placeCharacteristic->setValue("Anywhere");
    Serial.println("End Place");

    globalServicePtrW->start();

    globalServicePtrR = pServer->createService(SERVICER_UUID);

    temperatureCharacteristic = globalServicePtrR->createCharacteristic(
                                            TEMPERATURE_UUID,
                                            BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
                                        ); 
    temperatureCharacteristic->setAccessPermissions(GATT_PERM_READ); //  | GATT_PERM_NOTIF_IND

    temperatureCharacteristic->setValue("Temperature");
    temperatureCharacteristic->setCallbacks(new TemperatureCallbacks());
    Serial.println("End Temperature");


    humidityCharacteristic = globalServicePtrR->createCharacteristic(
                                            HUMIDITY_UUID,
                                            BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
                                        ); 
    humidityCharacteristic->setAccessPermissions(GATT_PERM_READ); //  | GATT_PERM_NOTIF_IND

    humidityCharacteristic->setValue("Humidity");
    humidityCharacteristic->setCallbacks(new HumidityCallbacks());
    Serial.println("End Humidity");


    globalServicePtrR->start();
    Serial.println("Service Started");
    logStartupStatus("BLE services started");

    // BLEAdvertising *pAdvertising = pServer->getAdvertising();  // this still is working for backward compatibility
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICEW_UUID);
    pAdvertising->addServiceUUID(SERVICER_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
    // pAdvertising->start();
    Serial.println("BLE Characteristics defined! Now you can read it in your phone!");
    logStartupStatus("BLE advertising active");

    // End Bluetooth Setup

    previousMillis = currentMillis - interval;
    Serial.println("Setup done");


    char* version = rpc_system_version();
    Serial.printf("RTL8720 Firmware Version: %s", rpc_system_version());
    Serial.println();
    erpc_free(version);

    // Show first environment values right after boot sequence.
    DynamicJsonDocument bootEnvData(4096);
    if (termosensor) {
        getEnvironmentData(&bootEnvData, 40);
    } else if (termosensor35) {
        getEnvironmentData(&bootEnvData, 35);
    }
    light = analogRead(WIO_LIGHT);
    endStartupStatus();
    delay(600);
    sendToScreen();
    screenMillis = millis();
    lastRefreshMillis = screenMillis;

}

void loop() {
  // put your main code here, to run repeatedly:
    currentMillis = millis();

    if (digitalRead(WIO_KEY_A) == LOW) {
        screenMillis = currentMillis;
        bscreenOn = false;
        Serial.println("screenmillis:" + String(screenMillis));
    } else if (digitalRead(WIO_KEY_B) == LOW && (currentMillis - wioKEYBMillis > 1000)) {
        wioKEYBMillis = currentMillis;
        bscreenOn = !bscreenOn;
        Serial.println("bscreenOn:" + String(bscreenOn));
    }


    if (currentMillis - previousMillis >= interval) {

        previousMillis = currentMillis; // update the last blink time
        
        // Get Environment Data
        DynamicJsonDocument envData(4096);
        if (termosensor){
            getEnvironmentData(&envData, 40);
        } 
        if(termosensor35){
            getEnvironmentData(&envData, 35);
        }
        light = analogRead(WIO_LIGHT);
        Serial.println("Luz:" + String(light));

        // Send results to server
        if(WiFi.status() == WL_CONNECTED){
                    // Periodically retry unsent backlog before sending the latest reading.
                    resendUnsentLogs(5);
                    int res = sendPostMessage(&envData);
                    if (res == 1){
                            writeDataLogFile(&envData, true);
                    }

            //Condition for low soil moisture
            // if(sensorValue < 50){
            //   spr.fillSprite(TFT_RED);
            //   spr.drawString("Time to water!",35,100);
            //   analogWrite(WIO_BUZZER, 150); //beep the buzzer
            //   delay(1000);
            //   analogWrite(WIO_BUZZER, 0); //Silence the buzzer
            //   delay(1000);
            // }

          // Disconnect to conserve energy and wait a bit before scanning again
          // WiFi.disconnect(); 
          // Serial.println("WiFi disconnected waiting " + String(seconds) + " seconds before resend.");
        } else {
                    ensureWiFiConnected();
          // TODO: Save results to send later when connection is available
          writeDataLogFile(&envData, true);
          Serial.println("WiFi not connected saving results to send later.");
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

    if ((currentMillis - screenMillis <= screenInterval && currentMillis - lastRefreshMillis >= 5000) || bscreenOn) {
      // getTime();
      digitalWrite(LCD_BACKLIGHT, HIGH);
      sendToScreen();
      lastRefreshMillis = currentMillis;
    } else if (digitalRead(LCD_BACKLIGHT) == HIGH && currentMillis - lastRefreshMillis >= screenInterval) {
      digitalWrite(LCD_BACKLIGHT, LOW);
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