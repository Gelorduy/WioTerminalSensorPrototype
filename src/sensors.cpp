#include "sensors.h"

#include <Arduino.h>
#include <string>
#include "app_state.h"

#ifdef NO_ERROR
#undef NO_ERROR
#endif
#define NO_ERROR 0

void getEnvironmentData(DynamicJsonDocument* jsonDoc, int sensorType) {
    aTemperature = 0.0;
    aHumidity = 0.0;
    now = rtc.now();
    uint32_t deviceSerial = serialNumber;

    if (sensorType == 40) {
        error = sensor.measureHighPrecision(aTemperature, aHumidity);
    } else {
        error = sensor35.read_meas_data_single_shot(HIGH_REP_WITH_STRCH, &aTemperature, &aHumidity);
        deviceSerial = -sensorType;
    }

    tnow = now.timestamp(DateTime::TIMESTAMP_FULL);
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
    Serial.print(String(tnow));
    Serial.print("\t");
    Serial.print("Serial: ");
    Serial.print(serialNumber);
    Serial.println();
    Serial.print("sensorHostMAC: ");
    Serial.print(String(strBaseMac));
    Serial.println();
}
