#ifndef GPS_MANAGER_H
#define GPS_MANAGER_H

#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>

TinyGPSPlus gps;
HardwareSerial SerialGPS(1);
String geocodedAddress = "";

void setupGPS() {
    SerialGPS.begin(9600, SERIAL_8N1, 16, 17);
    Serial.println("GPS 초기화 완료");
}

void readGPSData() {
    while (SerialGPS.available() > 0) {
        gps.encode(SerialGPS.read());
    }
}

void getGeocodedAddress(float latitude, float longitude) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String url = "https://maps.googleapis.com/maps/api/geocode/json?latlng=" +
                     String(latitude, 6) + "," + String(longitude, 6) +
                     "&key=YOUR_API_KEY";

        http.begin(url);
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            JSONVar jsonResponse = JSON.parse(payload);
            if (jsonResponse.hasOwnProperty("results") && jsonResponse["results"].length() > 0) {
                geocodedAddress = (const char*)jsonResponse["results"][0]["formatted_address"];
            } else {
                geocodedAddress = "주소 없음";
            }
        } else {
            geocodedAddress = "HTTP 요청 실패";
        }
        http.end();
    } else {
        geocodedAddress = "Wi-Fi 연결 실패";
    }
}

void processGPSData() {
    if (gps.location.isValid()) {
        getGeocodedAddress(gps.location.lat(), gps.location.lng());
    }
}

#endif
