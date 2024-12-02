#include <WiFi.h>
#include <TinyGPS++.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>
#include <HardwareSerial.h>
#include <WebServer.h>
#include <AWS_IOT.h>

// Wi-Fi 설정
const char* ssid = "MJS-Guest";
const char* password = "00908764801015";

// GPS 설정
TinyGPSPlus gps;
HardwareSerial SerialGPS(1); // UART1 사용 (RX=GPIO16, TX=GPIO17)

// AWS IoT 설정
AWS_IOT awsIot;
char HOST_ADDRESS[] = "a3pecuomf1y0cd-ats.iot.ap-northeast-2.amazonaws.com";
char CLIENT_ID[] = "WebServerDevice";
char SHADOW_UPDATE_DOCUMENTS_TOPIC[] = "$aws/things/ESP32_BIKEASSIST/shadow/update/accepted";
char TOPIC_ALERTS[] = "esp32/alerts"; // 알림 받는 토픽
char TOPIC_TELEMETRY[] = "esp32/telemetry"; // 주소, 속도 보내는 토픽 

char rcvdPayload[1024]; // AWS IoT에서 수신한 메시지
volatile int msgReceived = 0; // 메시지 수신 플래그
float awsSpeed = 0.0; // AWS에서 수신한 속도 데이터
String status = "";

unsigned long lastPrintTime = 0; // 마지막으로 유효성 상태를 출력한 시간
const unsigned long printInterval = 500; // .5초 간격으로 출력

String geocodedAddress = ""; // 변환된 주소
String receivedPayload = ""; // 수신된 데이터를 저장할 버퍼
String latestAlert = ""; // 수신된 Alert 메시지 저장

// 웹 서버
WebServer server(80);

// GPS 데이터를 JSON 형식으로 생성
String createGPSDataJSON() {
    String jsonData = "{";
    if (gps.location.isValid()) {
        jsonData += "\"latitude\":" + String(gps.location.lat(), 6) + ",";
        jsonData += "\"longitude\":" + String(gps.location.lng(), 6) + ",";
        jsonData += "\"address\":\"" + geocodedAddress + "\",";
        jsonData += "\"aws_speed\":" + String(awsSpeed, 2);
    } else {
        jsonData += "\"latitude\":0,\"longitude\":0,\"address\":\"GPS 데이터 없음\",";
        jsonData += "\"aws_speed\":0";
    }
    jsonData += "}";
    return jsonData;
}

// AWS에 주소 정보 업로드
void uploadAddressToAWS() {
    if (gps.location.isValid()) {
        String gpsData = createGPSDataJSON();
        String shadowUpdate = "{\"state\":{\"reported\":" + gpsData + "}}";

        // 디버깅: 생성된 JSON 문자열 출력
        Serial.println("Generated Shadow Update JSON:");
        Serial.println(shadowUpdate);

        // JSON 문자열 길이 확인
        if (shadowUpdate.length() > 512) { // AWS IoT 메시지 크기 제한 고려
            Serial.println("Error: Shadow Update JSON exceeds the size limit");
            return;
        }

        // String 객체를 char 배열로 변환
        char shadowUpdateChar[shadowUpdate.length() + 1];
        shadowUpdate.toCharArray(shadowUpdateChar, shadowUpdate.length() + 1);

        // AWS IoT로 전송
        int result = awsIot.publish("$aws/things/ESP32_BIKEASSIST/shadow/update", shadowUpdateChar); 
        if (result == 0) {
            Serial.println("AWS Shadow updated successfully with GPS data");
        } else {
            Serial.print("Failed to update AWS Shadow, error code: ");
            Serial.println(result);
        }
    } else {
        Serial.println("GPS location is not valid. Skipping upload.");
    }
}

// AWS IoT로 텔레메트리 데이터 전송
void sendTelemetryData(String address, float speed, String status) {
    char payload[256];
    snprintf(payload, sizeof(payload), "{\"address\": \"%s\", \"speed\": %.2f, \"status\": \"%s\"}", address.c_str(), speed, status.c_str());

    if (awsIot.publish(TOPIC_TELEMETRY, payload) == 0) {
        Serial.println("[AWS IoT] Telemetry data sent:");
        Serial.println(payload);
    } else {
        Serial.println("[AWS IoT] Failed to send telemetry data");
    }
}


// AWS 메시지 콜백 핸들러
void mySubCallBackHandler(char* topicName, int payloadLen, char* payLoad) {
    Serial.print("Received topic: ");
    Serial.println(topicName);
    Serial.print("Partial Payload: ");
    Serial.println(payLoad);

    // 수신된 데이터를 병합
    receivedPayload += String(payLoad);

    // JSON 유효성 검사 및 데이터 처리
    if (receivedPayload.startsWith("{") && receivedPayload.endsWith("}")) {
        JSONVar parsed = JSON.parse(receivedPayload); // 병합된 Payload 파싱

        if (JSON.typeof(parsed) == "undefined") {
            Serial.println("JSON Parsing failed");
            receivedPayload = ""; // 버퍼 초기화
            return;
        }

        // "reported" 섹션에서 데이터를 처리
        if (parsed.hasOwnProperty("state") && parsed["state"].hasOwnProperty("reported")) {
            JSONVar reported = parsed["state"]["reported"];

            // "speed" 값 처리
            if (reported.hasOwnProperty("speed")) {
                awsSpeed = static_cast<float>((double)reported["speed"]);
                Serial.print("AWS Speed updated: ");
                Serial.println(awsSpeed);
            }

            // "status" 값 처리
            if (reported.hasOwnProperty("status")) {
                status = (const char*)reported["status"];
                Serial.print("Received Status: ");
                Serial.println(status);

            } else {
                Serial.println("No status found in the reported section.");
            }
        }

        // "desired" 섹션에서 "led" 상태 확인
        if (parsed.hasOwnProperty("state") && parsed["state"].hasOwnProperty("desired")) {
            JSONVar desired = parsed["state"]["desired"];
            if (desired.hasOwnProperty("led")) {
                String ledState = (const char*)desired["led"];
                Serial.print("Desired LED State: ");
                Serial.println(ledState);

                // LED가 "ON"일 때 주소 정보 업로드
                if (ledState == "ON") {
                    uploadAddressToAWS();
                }
            }
        }


        if (JSON.typeof(parsed) == "object") { // JSON 객체인지 확인
            Serial.println("Parsed JSON Object: ");
            Serial.println(JSON.stringify(parsed)); // 파싱된 JSON 출력

            // "alert"와 "description" 속성 확인
            if (parsed.hasOwnProperty("alert") && parsed.hasOwnProperty("description")) {
                String alertMessage = (const char*)parsed["alert"];
                String description = (const char*)parsed["description"];

                // 디버깅용 데이터 출력
                Serial.println("Alert Message: " + alertMessage);
                Serial.println("Description: " + description);

                // JSON 생성
                JSONVar alertJson;
                alertJson["alert"] = alertMessage;
                alertJson["description"] = description;

                // `latestAlert`를 JSON 문자열로 저장
                latestAlert = JSON.stringify(alertJson);

                // 디버깅: 저장된 알림 데이터 출력
                Serial.println("Updated Latest Alert:");
                Serial.println(latestAlert);
            }
            else {
                Serial.println("Missing required properties in the payload:");
                if (!parsed.hasOwnProperty("alert")) {
                    Serial.println("- 'alert' property is missing.");
                }
                if (!parsed.hasOwnProperty("description")) {
                    Serial.println("- 'description' property is missing.");
                }
            }
        }
        else {
            Serial.println("Invalid JSON structure or type is not an object.");
            Serial.println("Received payload:");
            Serial.println(receivedPayload); // 디버깅용으로 원본 페이로드 출력
        }

        // 처리 완료 후 버퍼 초기화
        receivedPayload = "";
    }
    else {
        Serial.println("Payload is incomplete, waiting for the rest...");
    }
}



// GPS 데이터를 읽기
void readGPS() {
    while (SerialGPS.available() > 0) {
        gps.encode(SerialGPS.read());
    }
}

// Geocoding API 호출 및 주소 가져오기
void getGeocodedAddress(float latitude, float longitude) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String url = "https://maps.googleapis.com/maps/api/geocode/json?latlng=" +
                     String(latitude, 6) + "," + String(longitude, 6) +
                     "&key=AIzaSyDzSZz4EGR_CwxQ5Aa7ncwv85MrQmW_gkI&result_type=street_address&language=ko";

        http.begin(url);
        int httpCode = http.GET();

        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            Serial.println("Geocoding API 응답: ");
            JSONVar jsonResponse = JSON.parse(payload);
            if (JSON.typeof(jsonResponse) == "undefined") {
                Serial.println("JSON Parsing failed");
                geocodedAddress = "JSON 파싱 실패";
                return;
            }

            if (jsonResponse.hasOwnProperty("results") && jsonResponse["results"].length() > 0) {
                geocodedAddress = (const char*)jsonResponse["results"][0]["formatted_address"];
                Serial.println("Geocoded Address: " + geocodedAddress);
            } else {
                geocodedAddress = "주소 없음";
                Serial.println("주소 없음");
            }
        } else {
            Serial.println("HTTP Request failed, error: " + String(httpCode));
            geocodedAddress = "HTTP 요청 실패";
        }

        http.end();
    } else {
        Serial.println("Wi-Fi not connected");
        geocodedAddress = "Wi-Fi 연결 실패";
    }
}

// GPS 데이터를 주기적으로 처리
void processGPSData() {
    if (gps.location.isValid()) {
        getGeocodedAddress(gps.location.lat(), gps.location.lng());
    }
}

// HTML 페이지 JavaScript 코드
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
  <script src="https://maps.googleapis.com/maps/api/js?key=AIzaSyDzSZz4EGR_CwxQ5Aa7ncwv85MrQmW_gkI"></script>
  <script>
    let map, marker;

    function initMap() {
      const initialPosition = { lat: 0, lng: 0 };
      map = new google.maps.Map(document.getElementById("map"), {
        zoom: 15,
        center: initialPosition
      });
      marker = new google.maps.Marker({
        position: initialPosition,
        map: map
      });

      setInterval(fetchGPSData, 800);
      setInterval(fetchAlertData, 800);
    }

    function fetchGPSData() {
      fetch("/gps-data")
        .then(response => response.json())
        .then(data => {
          const { latitude, longitude, address, aws_speed } = data;

          if (latitude !== 0 && longitude !== 0) {
            const newPosition = { lat: latitude, lng: longitude };
            marker.setPosition(newPosition);
            map.setCenter(newPosition);

            document.getElementById("coordinates").innerText =
              `위도: ${latitude.toFixed(6)}, 경도: ${longitude.toFixed(6)}`;
            document.getElementById("address").innerText = `주소: ${address}`;
            document.getElementById("aws-speed").innerText = `속도: ${aws_speed.toFixed(2)} km/h`;
          } else {
            document.getElementById("coordinates").innerText = "GPS 데이터 불러오는중...";
            document.getElementById("address").innerText = "주소 없음";
            document.getElementById("aws-speed").innerText = "";
          }
        })
        .catch(error => console.error("Error fetching GPS data:", error));
    }

    function fetchAlertData() {
      fetch("/alerts")
        .then(response => response.json())
        .then(data => {
          const alert = data.alert || "알림: 대기 중";
          const description = data.description || "";

          document.getElementById("alert").innerText = `${alert}`;
          document.getElementById("description").innerText = `${description}`;
        })
        .catch(error => {
          console.error("Error fetching alert data:", error);
          document.getElementById("alert").innerText = "알림: 대기 중";
          document.getElementById("description").innerText = "";
        });
    }
  </script>
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

// 웹 서버 JSON 데이터 제공
void handleGPSData() {
    String json = createGPSDataJSON();
    server.send(200, "application/json", json);
}

void handleAlerts() {
    if (latestAlert.isEmpty()) {
        // 기본값으로 "알림: 대기 중" 메시지를 반환
        server.send(200, "application/json", "{\"alert\": \"알림: 대기 중\", \"description\": \"\"}");
    } else {
        // 최신 Alert 데이터를 반환
        server.send(200, "application/json", latestAlert);
    }
}



void setup() {
    Serial.begin(115200);
    SerialGPS.begin(9600, SERIAL_8N1, 16, 17);

    WiFi.disconnect(true);
    delay(1000);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Wi-Fi 연결 시도 중...");
    }
    Serial.println("Wi-Fi 연결 완료");
    Serial.print("ESP32 IP 주소: ");
    Serial.println(WiFi.localIP());

    delay(1000);
    if (awsIot.connect(HOST_ADDRESS, CLIENT_ID) == 0) {
        Serial.println("AWS IoT에 연결되었습니다");
        delay(1000);
        // Shadow Update 토픽 구독
        if (awsIot.subscribe(SHADOW_UPDATE_DOCUMENTS_TOPIC, mySubCallBackHandler) == 0) {
            Serial.println("Shadow 업데이트 주제 구독 성공");
        } else {
            Serial.println("Shadow 업데이트 주제 구독 실패");
        }
        delay(1000);
        // Alerts 토픽 구독
        if (awsIot.subscribe(TOPIC_ALERTS, mySubCallBackHandler) == 0) {
            Serial.println("Alerts 토픽 구독 성공");
        } else {
            Serial.println("Alerts 토픽 구독 실패");
        }
        delay(1000);
    } else {
        Serial.println("AWS IoT 연결 실패");
    }

    server.on("/", handleRoot);
    server.on("/gps-data", handleGPSData);
    server.on("/alerts", handleAlerts);

    server.begin();
    Serial.println("HTTP 서버 시작");
}

void loop() {
    readGPS();          // GPS 데이터 읽기
    unsigned long currentTime = millis();
    if (gps.location.isValid()) {
        Serial.println("GPS 위치가 유효합니다.");
        processGPSData();
    } else {
        // 마지막 출력 후 일정 시간이 지나면 유효하지 않은 상태 출력
        if (currentTime - lastPrintTime >= printInterval) {
            Serial.println("GPS 위치가 유효하지 않습니다.");
            lastPrintTime = currentTime; // 마지막 출력 시간 갱신
        }
    }

    // 페이로드에서 받은 속도와 주소로 Telemetry 데이터 송신
    if (!geocodedAddress.isEmpty()) {
        sendTelemetryData(geocodedAddress, awsSpeed, status); // AWS에서 받은 속도 활용
    }

    server.handleClient();  // 웹 서버 클라이언트 처리
}
