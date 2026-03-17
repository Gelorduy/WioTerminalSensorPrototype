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

static char errorMessage[64];
static int16_t error;
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


// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(const char* address) {
    for (int i = 0; i < NTP_PACKET_SIZE; ++i) {
        packetBuffer[i] = 0;
    }
    // Initialize values needed to form NTP request
    packetBuffer[0] = 0b11100011;   // LI, Version, Mode
    packetBuffer[1] = 0;     // Stratum, or type of clock
    packetBuffer[2] = 6;     // Polling Interval
    packetBuffer[3] = 0xEC;  // Peer Clock Precision
    // 8 bytes of zero for Root Delay & Root Dispersion
    packetBuffer[12] = 49;
    packetBuffer[13] = 0x4E;
    packetBuffer[14] = 49;
    packetBuffer[15] = 52;

    // all NTP fields have been given values, now you can send a packet requesting a timestamp:
    udp.beginPacket(address, 123); // NTP requests are to port 123
    udp.write(packetBuffer, NTP_PACKET_SIZE);
    udp.endPacket();
}


unsigned long getNTPtime() {
    // module returns a unsigned long time valus as secs since Jan 1, 1970 
    // unix time or 0 if a problem encounted

    //only send data when connected
    if (WiFi.status() == WL_CONNECTED) {
        udp.begin(WiFi.localIP(), localPort); //initializes the UDP state, This initializes the transfer buffer

        sendNTPpacket(timeServer); // send an NTP packet to a time server
        delay(1000); // wait to see if a reply is available

        if (udp.parsePacket()) {
            Serial.println("udp packet received");
            Serial.println("");
            udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

            //the timestamp starts at byte 40 of the received packet and is four bytes, or two words, long. First, extract the two words:
            unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
            unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
            // combine the four bytes (two words) into a long integer this is NTP time (seconds since Jan 1 1900):
            unsigned long secsSince1900 = highWord << 16 | lowWord;
            const unsigned long seventyYears = 2208988800UL;             // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
            unsigned long epoch = secsSince1900 - seventyYears;

            // adjust time for timezone offset in secs +/- from UTC
            // Mexico time offset from UTC is -6 hours (-21,600 secs)
            // + East of GMT
            // - West of GMT
            long tzOffset = -21600UL;

            // Mexico local time 
            unsigned long adjustedTime;
            return adjustedTime = epoch + tzOffset;
        }
        else {
            // were not able to parse the udp packet successfully clear down the udp connection
            udp.stop();
            return 0; // zero indicates a failure
        }
        udp.stop(); // not calling ntp time frequently, stop releases resources
    }
    else {
        // network not connected
        return 0;
    }

}


void scanNetworks(DynamicJsonDocument* jsonDoc){
    Serial.println("scan start");

    // WiFi.scanNetworks will return the number of networks found
    if(WiFi.status() == WL_DISCONNECTED){
      int n = WiFi.scanNetworks();
      Serial.println("scan done");
      if (n == 0) {
          Serial.println("no networks found");
      } else {
          Serial.print(n);
          Serial.println(" networks found");
          for (int i = 0; i < n; ++i) {
              // Calculate Distance
              distance = pow(10,((K - (20 * log10(channels[WiFi.channel(i)-1])) + abs(WiFi.RSSI(i)))/20));

              // Load Object with relevant data
              (*jsonDoc).createNestedObject("ap" + String( i ));
              (*jsonDoc)["ap"  + String( i )]["name"] = WiFi.SSID(i);
              (*jsonDoc)["ap"  + String( i )]["signal"] = WiFi.RSSI(i);
              (*jsonDoc)["ap"  + String( i )]["channel"] = WiFi.channel(i);
              (*jsonDoc)["ap"  + String( i )]["distance"] = distance;
              (*jsonDoc)["ap"  + String( i )]["encriptionType"] = WiFi.encryptionType(i);
              (*jsonDoc)["ap"  + String( i )]["sensorHostMAC"] = strBaseMac;

              if (WiFi.SSID(i) == ssid){
                if (wifissid == 2){
                  wifissid = 3; // 0=Unknown, 1=Primary, 2=Alternate, 3=Both, 4=Mobile, 5=NotFound
                } else {
                  wifissid = 1; // 0=Unknown, 1=Primary, 2=Alternate, 3=Both, 4=Mobile, 5=NotFound
                }

              } else if (WiFi.SSID(i) == alternatessid){
                if (wifissid == 1){
                  wifissid = 3; // 0=Unknown, 1=Primary, 2=Alternate, 3=Both, 4=Mobile, 5=NotFound
                } else {
                  wifissid = 2; // 0=Unknown, 1=Primary, 2=Alternate, 3=Both, 4=Mobile, 5=NotFound
                }
              } else {
                if (WiFi.SSID(i) == mobilessid){
                  wifissid = 4; // 0=Unknown, 1=Primary, 2=Alternate, 3=Both, 4=Mobile, 5=NotFound
                } else if(wifissid == 0){
                  wifissid = 5; // 0=Unknown, 1=Primary, 2=Alternate, 3=Both, 4=Mobile, 5=NotFound
                }
              }
          }
      }
        // Serial.println("");
        // Serial.println(serializeJsonPretty(jsonDoc, Serial));

    }

}

void connectWiFi(){

    Serial.print("wifissid: ");
    Serial.println(String(wifissid));

    const char* issid = "";
    const char* ipwd = "";

    if (wifissid != 1 && wifissid != 2 && wifissid != 3) {
        Serial.print("Error Rescanning Networks "); 
        DynamicJsonDocument scanData(4096);
        scanNetworks(&scanData);
    } else {
        if(WiFi.status() == WL_DISCONNECTED){
        // Let's connect to wifi
            if (wifissid == 1 || wifissid == 3){
              issid = ssid;
              ipwd = password;
              Serial.print("Primary Wifi connecting "); 
            } else if (wifissid == 2){
              issid = alternatessid;
              ipwd = alternatepassword;
              Serial.print("Alternate Wifi connecting "); 
            }

            WiFi.begin(issid, ipwd);
            while (WiFi.status() != WL_CONNECTED) {
                currentMillis = millis();
                if (currentMillis - previousMillisC >= 500){
                    previousMillisC = currentMillis; // update the last blink time
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

int sendPostMessage(DynamicJsonDocument* document){

    if(WiFi.status()== WL_CONNECTED){
        String message = "";
        serializeJson(*document, message);

    // Serial.println("\nStarting connection to server...");
    // if (!client.connect("thermolog.gonmobile.com", 443)) {
    //     Serial.println("Connection failed!");
    // } else {
    //     Serial.println("Connected to server!");
    // }
    //     String furl = String(SERVERURL) + String(SVRPATH);
    //     // Make a HTTP POST request:
    //     client.println("POST https://" + furl + " HTTP/1.1");
    //     client.println("Host: " + String(SERVERURL));
    //     client.println(F("User-Agent: WioProcomsa"));
    //     client.println(F("Accept: application/json;"));
    //     client.println("Authorization: Bearer " + String(BEARERTOKEN));
    //     client.println(F("Connection: close"));
    //     client.println(F("Content-Type: application/json;"));
    //     client.print(F("Content-Length: "));
    //     client.println(message.length());
    //     client.println();
    //     client.println(message);
    //     client.println();

    //     while (client.connected()) {
    //         String line = client.readStringUntil('\n');
    //         if (line == "\r") {
    //             Serial.println("headers received");
    //             break;
    //         }
    //     }
    //     // if there are incoming bytes available
    //     // from the server, read them and print them:
    //     while (client.available()) {
    //         char c = client.read();
    //         if (c == '\n') {
    //             Serial.write('\r');
    //         }
    //         Serial.write(c);
    //     }
    //     client.stop();
    // }

/* 
POST /echo/post/json HTTP/1.1
Host: reqbin.com
Accept: application/json
Authorization: Bearer {token}
Content-Type: application/json
Content-Length: 61
Connection: keep-alive

    if (client.connect(host, 443)) {
      client.println("POST " + url + " HTTP/1.0");
      client.println("Host: " + (String)host);
      client.println(F("User-Agent: ESP"));
      client.println(F("Connection: close"));
      client.println(F("Content-Type: application/x-www-form-urlencoded;"));
      client.print(F("Content-Length: "));
      client.println(data.length());
      client.println();
      client.println(data);

{
	"employee":{ "name":"Emma", "age":28, "city":"Boston" }
}
*/
      // if(client.connected()){
      //   {

          HTTPClient http; 
          http.setReuse(false);
          Serial.println("Begin Connection...");
          int svrResponse = http.begin(SERVERURL, root_ca);
          // http.begin(client, SERVERURL)
          // http.begin(SERVERURL, root_ca); //, root_ca
          Serial.println("Begin response:");
          Serial.println(svrResponse);
          http.addHeader("Content-Type", "application/json; charset=utf-8");
          http.addHeader("Authorization","Bearer " + String(BEARERTOKEN));

          Serial.println("Sending Post message...");
          int httpResponseCode = http.POST( message);
          Serial.println("Post Message Sent and received response...");

          Serial.println("Response Code: " + String(httpResponseCode));
          if(httpResponseCode>0){
              String response = http.getString();
              Serial.println(httpResponseCode);
              Serial.println(response);
          }else{
              Serial.print("Error on sending Information to Server: ");
              Serial.println(httpResponseCode);
              http.end();
              return 1;
          }
          http.end();
      //   }
      // } else {
      //     Serial.println("Unable to create client");
      // }
    } else{
        Serial.println("Error in WiFi connection");   
    }
    return 0;
}

void sendToScreen(){

    // Serial.println("Entering Screen Display.");

    // Serial.println("Generating Header.");
/*    
    //Setting the title header 
    spr.fillSprite(TFT_DARKGREY); //Fill background with dark gray color
    spr.fillRect(0,0,320,50,TFT_PROCOMSABLUE); //Rectangle fill with dark green 
    spr.setTextColor(TFT_WHITE); //Setting text color
    // spr.setTextSize(3); //Setting text size 
    // spr.pushImage(20, 10, 32, 32,"Logo_32_32.bmp");
    spr.setFreeFont(FSSO12);
    spr.drawString("Termowatch",50,10); //Drawing a text string 
    // spr.setTextSize(1); //Setting text size 
    spr.setFreeFont(FSSO9);
    spr.drawString("Procomsa",50,29); //Drawing a text string 
    // spr.drawString(sprintf("%b %d %H:%M", &tCurrInfo),210,10); //Drawing time :%S 
    // spr.drawString(sprintf("Luz: %i %", '50'),230,29); //Drawing a text string 
    spr.drawString("Jul 06 17:07",210,10); //Drawing time :%S 
    spr.drawString("Luz: 20 %",230,29); //Drawing a text string 


    Serial.println("Generating Lines.");
    spr.drawFastVLine(160,50,190,TFT_PROCOMSABLUE); //Drawing verticle line
    spr.drawFastHLine(0,130,320,TFT_PROCOMSABLUE); //Drawing horizontal line

    Serial.println("Setting Temp.");
    //Setting temperature
    spr.setFreeFont(FSSO12);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("Temp °C",52,40);
    spr.setFreeFont(FSSO24);
    spr.drawNumber(aTemperature,50,95); //Display temperature values 
    spr.drawString("C",90,95);


    Serial.println("Setting Humidity.");
    //Setting humidity
    spr.setFreeFont(FSSO12);
    spr.drawString("Hum Rel %",52,190);
    spr.setFreeFont(FSSO24);
    spr.drawNumber(aHumidity,30,190); //Display humidity values 
    spr.drawString("%RH",70,190);

    // //Setting soil moisture
    // sensorValue = analogRead(sensorPin); //Store sensor values 
    // sensorValue = map(sensorValue,1023,400,0,100); //Map sensor values 
    // spr.setTextSize(2);
    // spr.drawString("Soil Moisture",160,65);
    // spr.setTextSize(3);
    // spr.drawNumber(sensorValue,200,95); //Display sensor values as percentage  
    // spr.drawString("%",240,95);
    
    // //Setting light 
    // spr.setTextSize(2);
    // spr.drawString("Light",200,160);
    // spr.setTextSize(3);
    // light = map(light,0,1023,0,100); //Map sensor values 
    // spr.drawNumber(light,205,190); //Display sensor values as percentage  
    // spr.drawString("%",245,190);

    // //Condition for low soil moisture
    // if(sensorValue < 50){
    //   spr.fillSprite(TFT_RED);
    //   spr.drawString("Time to water!",35,100);
    //   analogWrite(WIO_BUZZER, 150); //beep the buzzer
    //   delay(1000);
    //   analogWrite(WIO_BUZZER, 0); //Silence the buzzer
    //   delay(1000);
    // }

    spr.pushSprite(0,0); //Push to LCD
*/


    //Setting the title header 
    tft.fillScreen(TFT_BLACK); //Fill background with dark gray color
    tft.fillRect(0, 0, 320, 55, TFT_PROCOMSABLUE); //Rectangle fill with dark green 
    tft.setTextColor(TFT_WHITE); //Setting text color
//    drawImage<u_int16_t>("Procomsa_fi_whiteTxt_32x32.bmp", 15, 10);
    tft.setFreeFont(FSSO12);
    tft.drawString("Termocheck", 50, 10); //Drawing Gadget Name 
    tft.setFreeFont(FSSO9);
    String location = "GGG-" + String(placeCharacteristic->getValue().c_str());
    tft.drawString(location, 50, 35); //Drawing a text string 
    if(tnow == ""){
      now = rtc.now();
    }
    char* fmt = new char[12];
    strcpy(fmt, "MMM DD hh:mm");
    tnow = now.toString(fmt);
    tft.drawString(tnow, 210, 10); //Drawing time :%S  "Jul 06 17:07" 
    String sLight = "Luz: " + String(light) + " %";
    tft.drawString(sLight, 230, 35); //Drawing a text string 


    // Serial.println("Generating Lines.");client.connected()
    tft.drawFastVLine(160, 55, 95, TFT_PROCOMSABLUE); //Drawing verticle line
    tft.drawFastHLine(0, 150, 320, TFT_PROCOMSABLUE); //Drawing horizontal line

    // Serial.println("Setting Temp.");
    //Setting temperature
    tft.setFreeFont(FSSO12);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Temp C", 35, 63);
    tft.setFreeFont(FSSO24);
    tft.drawFloat(aTemperature, 2, 20, 100); //Display temperature values 


    // Serial.println("Setting Humidity.");
    //Setting humidity
    tft.setFreeFont(FSSO12);
    tft.drawString("Hum Rel %", 180, 63);
    tft.setFreeFont(FSSO24);
    tft.drawFloat(aHumidity, 2, 180, 100); //Display humidity values 

    delay(1500);


}

void getEnvironmentData(DynamicJsonDocument* jsonDoc, int sensorType = 40){
    aTemperature = 0.0;
    aHumidity = 0.0;
    // unsigned long time = 0; //getTime();
    now = rtc.now();
    uint32_t deviceSerial = serialNumber;

    if (sensorType == 40){
        error = sensor.measureHighPrecision(aTemperature, aHumidity);
    } else {
        error = sensor35.read_meas_data_single_shot(HIGH_REP_WITH_STRCH, &aTemperature, &aHumidity);
        deviceSerial = -sensorType;
    }

    tnow = now.timestamp(DateTime::TIMESTAMP_FULL);
    // Serial.println("Unixtime: " + String(now.unixtime()));
    if (error != NO_ERROR) {
        Serial.print("Error trying to execute measureHighestPrecision(): ");
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.println(errorMessage);
        return;
    }

    (*jsonDoc)["environmentData"] = "readings";
    (*jsonDoc).createNestedObject("reading");
    (*jsonDoc)["reading"]["temp"] = aTemperature;
    (*jsonDoc)["reading"]["tempStr"] = String(aTemperature);
    (*jsonDoc)["reading"]["tempUnit"] = "Centigrade";
    (*jsonDoc)["reading"]["relHumi"] = aHumidity;
    (*jsonDoc)["reading"]["relHumiStr"] = String(aHumidity);
    (*jsonDoc)["reading"]["relHumiUnit"] = "Percentage";
    (*jsonDoc)["reading"]["time"] = String(now.unixtime());
    (*jsonDoc)["reading"]["timeString"] = tnow;
    (*jsonDoc)["reading"]["serial"] = deviceSerial;
    (*jsonDoc)["reading"]["sensorHostMAC"] = strBaseMac;

    temperatureCharacteristic->setValue(std::to_string(aTemperature));
    temperatureCharacteristic->notify();

    humidityCharacteristic->setValue(std::to_string(aHumidity));
    humidityCharacteristic->notify();

    Serial.print("Temperature: ");
    Serial.print(aTemperature);
    Serial.print("\t");
    Serial.print("Humidity: ");
    Serial.print(aHumidity);
    Serial.println();
    Serial.print("Time: ");
    Serial.print(String( tnow ));
    Serial.print("\t");
    Serial.print("Serial: ");
    Serial.print(serialNumber);
    Serial.println();
    Serial.print("sensorHostMAC: ");
    Serial.print(String( strBaseMac ));
    Serial.println();

}

void writeDataLogFile(DynamicJsonDocument* jsonDoc, bool unSent){
    // open the file. note that only one file can be open at a time,
    // so you have to close this one before opening another.

    String logName = "readings" + String(serialNumber) + ".log"; // The path to read and write files needs to start with "/"
    bool logCreated = syslogCreated;

    if (unSent){
        logName = "unsent" + String(serialNumber) + ".log";
        logCreated = unsentlogCreated;
    }
    Serial.println("Opening file: " + logName);

    if(logCreated){
        LogFile = SD.open(logName, FILE_APPEND);
        Serial.println("Appending file: " + logName);
    } else {
        LogFile = SD.open(logName, FILE_WRITE);
        Serial.println("Creating file: " + logName);
        if(unSent){
            unsentlogCreated = true;
        } else{
            syslogCreated = true;
        }
    }

    // if the file opened okay, write to it:
    if (LogFile) {
        Serial.println("Writing to LogFile File: " + logName);
        String message = "";
        serializeJson(*jsonDoc, message);
        LogFile.println("{");
        LogFile.print(message);
        LogFile.println("},");
        // close the file:
        LogFile.close();
        Serial.println("done.");
    } else {
        // if the file didn't open, print an error:
        Serial.println("error opening LogFile" + logName);
    }

}


void setup() {
    Serial.begin(9600);

    // Set PIN Modes
    pinMode(WIO_LIGHT, INPUT); //Set light sensor pin as INPUT
    pinMode(WIO_BUZZER, OUTPUT); //Set buzzer pin as OUTPUT

 
    // Set WiFi to station mode and disconnect from an AP if it was previously connected
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.disconnect();
    
    // Start Screen
    tft.begin();
    tft.setRotation(3);
    spr.createSprite(TFT_HEIGHT, TFT_WIDTH); //Create buffer

    // SDCard Setup
    Serial.println("Initializing SDCard...");
    if (!SD.begin(SDCARD_SS_PIN, SDCARD_SPI)) {
        Serial.println("Initialization failed!");
        while(1);
    } else{
        Serial.println("File initialization done.");
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
    sensor.begin(Wire, SHT40_I2C_ADDR_44);
    sensor.softReset();
    delay(10);
    error = sensor.serialNumber(serialNumber);
    
    if (error != NO_ERROR) {
        Serial.print("Error trying to execute serialNumber(): ");
        errorToString(error, errorMessage, sizeof errorMessage);
        Serial.println(errorMessage);
    } else {
        termosensor = true;
    }
    Serial.print("serialNumberSHT40: ");
    Serial.print(serialNumber);
    Serial.println();

    // SHT35 Sensor HT Sensor
    if (sensor35.init() == NO_ERROR){
        error = sensor35.read_meas_data_single_shot(HIGH_REP_WITH_STRCH, &aTemperature, &aHumidity);

        Serial.println("Sensor Read Result: " + String(errorMessage));

        if (error != NO_ERROR){
            termosensor35 = true;
        }
        Serial.println("SHT35 sensor initialization successful :" + termosensor35);
    } else {
        Serial.println("SHT35 sensor initialization failed");
    }




    // WiFi Setup
    DynamicJsonDocument scanData(4096); //4096
    scanNetworks(&scanData);
    if (wifissid != 1 && wifissid != 2 && wifissid != 3) {
      Serial.print("No Available Wifi networks found."); 

    } else {
      // char* version = rpc_system_version();
      // Serial.print("RTL8720 Firmware Version: ");
      // Serial.println(version);
      // erpc_free(version);

      connectWiFi();
      strBaseMac = WiFi.macAddress();
      Serial.print("MAC address: ");
      Serial.println(strBaseMac);

      // get the time via NTP (udp) call to time server
      // getNTPtime returns epoch UTC time adjusted for timezone but not daylight savings
      // time
      devicetime = getNTPtime();

      // check if rtc present
      if (devicetime == 0) {
          Serial.println("Failed to get time from network time server.");
      }

      if (!rtc.begin()) {
          Serial.println("Couldn't find RTC");
          while (1) delay(10); // stop operating
      }

      // get and print the current rtc time
      now = rtc.now();
      Serial.print("RTC time is: ");
      Serial.println(now.timestamp(DateTime::TIMESTAMP_FULL));

      // adjust time using ntp time
      rtc.adjust(DateTime(devicetime));

      // print boot update details
      Serial.println("RTC (boot) time updated.");
      // get and print the adjusted rtc time
      now = rtc.now();
      Serial.print("Adjusted RTC (boot) time is: ");
      Serial.println(now.timestamp(DateTime::TIMESTAMP_FULL));

    }

    // // Usr button Setup
    // pinMode(buttonPin, INPUT_PULLUP);
    pinMode(WIO_LIGHT, INPUT);
    pinMode(WIO_KEY_A, INPUT_PULLUP);
    pinMode(WIO_KEY_B, INPUT_PULLUP);

    client.setCACert(root_ca);
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

    // End Bluetooth Setup

    previousMillis = currentMillis - interval;
    Serial.println("Setup done");


    char* version = rpc_system_version();
    Serial.printf("RTL8720 Firmware Version: %s", rpc_system_version());
    Serial.println();
    erpc_free(version);

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
          // sendoldReadings First
          // TODO: Send old readings before sending current data
            // int res = sendPostMessage(&envData);
            // if (res == 1){
            //     writeDataLogFile(&envData, true);
            // }

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
          // TODO: Save results to send later when connection is available
          writeDataLogFile(&envData, true);
          Serial.println("WiFi not connected saving results to send later.");
        }
        writeDataLogFile(&envData, false);

        std::string rxValue = placeCharacteristic->getValue();
        String test = String(rxValue.c_str());
        Serial.println(test);

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