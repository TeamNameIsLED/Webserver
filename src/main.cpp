#include <WiFi.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <WebServer.h>
#include <AWS_IOT.h>
#include <Arduino_JSON.h>

// Wi-Fi 설정
const char* ssid = "KIMBO";
const char* password = "03011777";

// GPS 설정
TinyGPSPlus gps;
HardwareSerial SerialGPS(1); // UART1 사용 (RX=GPIO16, TX=GPIO17)

// AWS IoT 설정
AWS_IOT awsIot;
char HOST_ADDRESS[] = "a3pecuomf1y0cd-ats.iot.ap-northeast-2.amazonaws.com";
char CLIENT_ID[] = "WebServerDevice";
char SHADOW_UPDATE_DOCUMENTS_TOPIC[] = "$aws/things/ESP32_BIKEASSIST/shadow/update/accepted";

char rcvdPayload[1024]; // AWS IoT에서 수신한 메시지
volatile int msgReceived = 0; // 메시지 수신 플래그
float awsSpeed = 0.0; // AWS에서 수신한 속도 데이터

// 웹 서버
WebServer server(80);

// 주기적 업데이트를 위한 변수
unsigned long lastUpdateTime = 0;
const unsigned long updateInterval = 500; // 업데이트 간격 (0.5초)

// AWS Shadow 업데이트 수신 콜백
void mySubCallBackHandler(char* topicName, int payloadLen, char* payLoad) {
    Serial.print("Received topic: ");
    Serial.println(topicName);
    Serial.print("Payload: ");
    Serial.println(payLoad);

    strncpy(rcvdPayload, payLoad, payloadLen);
    rcvdPayload[payloadLen] = 0;
    msgReceived = 1; // 메시지 수신 플래그 설정
}

// GPS 데이터를 JSON 형식으로 생성
String createGPSDataJSON() {
    String jsonData = "{";
    if (gps.location.isValid()) {
        jsonData += "\"latitude\":" + String(gps.location.lat(), 6) + ",";
        jsonData += "\"longitude\":" + String(gps.location.lng(), 6) + ",";
        jsonData += "\"satellites\":" + String(gps.satellites.value()) + ",";
        jsonData += "\"hdop\":" + String(gps.hdop.value()) + ",";
    } else {
        jsonData += "\"latitude\":0,\"longitude\":0,\"satellites\":0,\"hdop\":0,";
    }
    jsonData += "\"aws_speed\":" + String(awsSpeed, 2); // AWS 속도 추가
    jsonData += "}";
    return jsonData;
}

// GPS 데이터 읽기
void readGPS() {
    while (SerialGPS.available() > 0) {
        gps.encode(SerialGPS.read());
    }
}

// GPS 데이터를 주기적으로 처리
void processGPSData() {
    unsigned long currentTime = millis();
    if (currentTime - lastUpdateTime >= updateInterval) {
        lastUpdateTime = currentTime;

        // 위치 데이터 출력
        if (gps.location.isValid()) {
            Serial.print("위도: ");
            Serial.println(gps.location.lat(), 6);
            Serial.print("경도: ");
            Serial.println(gps.location.lng(), 6);
        } else {
            Serial.println("GPS 신호를 찾을 수 없습니다.");
        }
    }
}

// 웹 서버에서 최신 데이터 제공
void handleGPSData() {
    // AWS Shadow 메시지가 처리되지 않았다면 즉시 처리
    if (msgReceived == 1) {
        msgReceived = 0;
        JSONVar parsed = JSON.parse(rcvdPayload);
        if (JSON.typeof(parsed) != "undefined" && parsed["state"]["reported"].hasOwnProperty("speed")) {
            awsSpeed = static_cast<float>((double)parsed["state"]["reported"]["speed"]);
        }
    }

    // 최신 GPS 데이터를 JSON으로 생성
    String json = createGPSDataJSON();
    server.send(200, "application/json", json);
}

// HTML 페이지 제공
void handleRoot() {
    server.send(200, "text/html; charset=utf-8", R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32 Tracker</title>
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

      setInterval(fetchGPSData, 500); // 0.5초마다 GPS 데이터 갱신
    }

    function fetchGPSData() {
      fetch("/gps-data")
        .then(response => response.json())
        .then(data => {
          const lat = data.latitude;
          const lng = data.longitude;
          const satellites = data.satellites;
          const awsSpeed = data.aws_speed;

          if (lat !== 0 && lng !== 0) {
            const newPosition = { lat, lng };
            marker.setPosition(newPosition);
            map.setCenter(newPosition);
            document.getElementById("coordinates").innerText =
              `위도: ${lat}, 경도: ${lng}, 위성: ${satellites}, AWS 속도: ${awsSpeed} km/h`;
          } else {
            document.getElementById("coordinates").innerText =
              "GPS 데이터를 가져올 수 없습니다.";
          }
        })
        .catch(error => console.error("Error fetching GPS data:", error));
    }
  </script>
</head>
<body onload="initMap()">
  <h1>ESP32 GPS & AWS Tracker</h1>
  <div id="map" style="width: 90%; height: 500px;"></div>
  <p id="coordinates">데이터 로딩 중...</p>
</body>
</html>
)rawliteral");
}

void setup() {
    Serial.begin(115200);
    SerialGPS.begin(9600, SERIAL_8N1, 16, 17);

    // Wi-Fi 연결
    WiFi.disconnect(true);
    delay(1000);
    WiFi.mode(WIFI_STA);
    delay(1000);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Wi-Fi 연결 시도 중...");
    }
    Serial.println("Wi-Fi 연결 완료");
    Serial.print("ESP32 IP 주소: ");
    Serial.println(WiFi.localIP());
    delay(1000);

    // AWS IoT 연결
    if (awsIot.connect(HOST_ADDRESS, CLIENT_ID) == 0) {
        Serial.println("AWS IoT에 연결되었습니다");
        delay(1000);
        // Shadow 업데이트 주제 구독
        if (awsIot.subscribe(SHADOW_UPDATE_DOCUMENTS_TOPIC, mySubCallBackHandler) == 0) {
            Serial.println("Shadow 업데이트 주제 구독 성공");
        } else {
            Serial.println("Shadow 업데이트 주제 구독 실패");
            while (1);
        }
    } else {
        Serial.println("AWS IoT 연결 실패");
        while (1);
    }

    // 웹 서버 경로 설정
    server.on("/", handleRoot);
    server.on("/gps-data", handleGPSData);
    server.begin();
    Serial.println("HTTP 서버 시작");
}

void loop() {
    // GPS 데이터 읽기 및 처리
    readGPS();
    processGPSData();

    // 웹 서버 요청 처리
    server.handleClient();
}
