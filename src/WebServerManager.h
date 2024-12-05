#ifndef WEB_SERVER_MANAGER_H
#define WEB_SERVER_MANAGER_H

#include <WebServer.h>
#include "GPSManager.h"
#include "AWSManager.h"

WebServer server(80);

void handleRoot() {
    server.send(200, "text/html; charset=utf-8", R"rawliteral(
<!DOCTYPE html>
<html lang="ko">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 GPS & AWS Tracker</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      margin: 0;
      padding: 0;
      background-color: #f4f4f4;
      color: #333;
    }
    header {
      background-color: #0078d4;
      color: white;
      padding: 10px 20px;
      text-align: center;
    }
    main {
      max-width: 800px;
      margin: 20px auto;
      padding: 20px;
      background: white;
      border-radius: 8px;
      box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1);
    }
    h1 {
      margin-bottom: 10px;
    }
    #map {
      width: 100%;
      height: 400px;
      border: 1px solid #ccc;
      border-radius: 8px;
    }
    .info {
      margin: 15px 0;
      padding: 10px;
      background: #f9f9f9;
      border: 1px solid #ddd;
      border-radius: 5px;
    }
    .info p {
      margin: 5px 0;
    }
    footer {
      text-align: center;
      margin-top: 20px;
      color: #666;
    }
  </style>
</head>
<body onload="initMap()">
  <header>
    <h1>조명은 LED,<br>LED로 밝히는 어린이 안전!</h1>
  </header>
  <main>
    <div id="map"></div>
    <div class="info">
      <p id="coordinates">위도, 경도 로딩 중...</p>
      <p id="address">주소 로딩 중...</p>
      <p id="aws-speed">속도 로딩 중...</p>
    </div>
    <div class="info">
      <p id="alert">알림 로딩 중...</p>
      <p id="description"></p>
    </div>
  </main>
  <footer>
    <p>&copy; 2024 TeamNameIsLED</p>
  </footer>
</body>
</html>
)rawliteral");
}


void handleGPSData() {
    String json = "{";
    if (gps.location.isValid()) {
        json += "\"latitude\":" + String(gps.location.lat(), 6) + ",";
        json += "\"longitude\":" + String(gps.location.lng(), 6) + ",";
        json += "\"address\":\"" + geocodedAddress + "\",";
        json += "\"aws_speed\":" + String(awsSpeed, 2);
    } else {
        json += "\"latitude\":0,\"longitude\":0,\"address\":\"GPS 데이터 없음\",\"aws_speed\":0";
    }
    json += "}";
    server.send(200, "application/json", json);
}

void handleAlerts() {
    server.send(200, "application/json", latestAlert.isEmpty()
                  ? "{\"alert\": \"알림: 대기 중\", \"description\": \"\"}"
                  : latestAlert);
}

void setupWebServer() {
    server.on("/", handleRoot);
    server.on("/gps-data", handleGPSData);
    server.on("/alerts", handleAlerts);
    server.begin();
}

void handleWebRequests() {
    server.handleClient();
}

#endif
