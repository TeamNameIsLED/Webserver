#include "WiFiManager.h"
#include "GPSManager.h"
#include "AWSManager.h"
#include "WebServerManager.h"

void setup() {
    Serial.begin(115200);
    setupWiFi();
    setupAWS();
    setupGPS();
    setupWebServer();
    Serial.println("시스템 초기화 완료");
}

void loop() {
    readGPSData();    // GPS 데이터 읽기
    processGPSData(); // GPS 데이터를 처리
    handleAlerts();   // 부저 및 알림 처리
    sendTelemetry(geocodedAddress);  // AWS IoT로 텔레메트리 데이터 송신
    handleWebRequests(); // 웹 요청 처리
}
