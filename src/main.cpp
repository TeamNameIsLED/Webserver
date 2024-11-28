#include <WiFi.h>
#include <TinyGPS++.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>
#include <HardwareSerial.h>
#include <WebServer.h>
#include <AWS_IOT.h>

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

String geocodedAddress = ""; // 변환된 주소

// 웹 서버
WebServer server(80);

// 주기적 업데이트를 위한 변수
unsigned long lastUpdateTime = 0;
const unsigned long updateInterval = 500; // 업데이트 간격 (0.5초)

/// 수신된 데이터를 저장할 버퍼
String receivedPayload = "";

void mySubCallBackHandler(char* topicName, int payloadLen, char* payLoad) {
    Serial.print("Received topic: ");
    Serial.println(topicName);
    Serial.print("Partial Payload: ");
    Serial.println(payLoad);

    // 수신된 데이터를 병합
    receivedPayload += String(payLoad);

    // JSON 유효성 검사
    if (receivedPayload.startsWith("{") && receivedPayload.endsWith("}")) {
        msgReceived = 1; // 메시지 수신 플래그 설정
    } else {
        Serial.println("Payload is incomplete, waiting for the rest...");
    }
}


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

        if (gps.location.isValid()) {
            Serial.print("위도: ");
            Serial.println(gps.location.lat(), 6);
            Serial.print("경도: ");
            Serial.println(gps.location.lng(), 6);

            // Geocoding API 호출
            getGeocodedAddress(gps.location.lat(), gps.location.lng());
        } else {
            Serial.println("GPS 신호를 찾을 수 없습니다.");
            geocodedAddress = "GPS 신호 없음";
        }
    }
}

// AWS IoT Shadow 데이터 업데이트
void processAWSData() {
    if (msgReceived == 1) {
        msgReceived = 0; // 메시지 수신 플래그 해제
        JSONVar parsed = JSON.parse(receivedPayload); // 병합된 Payload 파싱

        if (JSON.typeof(parsed) == "undefined") {
            Serial.println("JSON Parsing failed");
            receivedPayload = ""; // 버퍼 초기화
            return;
        }

        // "reported" 섹션에서 "speed" 값 가져오기
        if (parsed.hasOwnProperty("state") && parsed["state"].hasOwnProperty("reported")) {
            JSONVar reported = parsed["state"]["reported"];
            if (reported.hasOwnProperty("speed")) {
                awsSpeed = static_cast<float>((double)reported["speed"]);
                Serial.print("AWS Speed: ");
                Serial.println(awsSpeed);
            } else {
                Serial.println("Speed data not found in reported section");
            }
        }

        // "desired" 섹션에서 추가 처리 (필요한 경우)
        if (parsed.hasOwnProperty("state") && parsed["state"].hasOwnProperty("desired")) {
            JSONVar desired = parsed["state"]["desired"];
            // 원하는 작업 추가 가능
            if (desired.hasOwnProperty("led")) {
                String ledState = (const char*)desired["led"];
                Serial.print("Desired LED State: ");
                Serial.println(ledState);
            }
        }

        // 처리 완료 후 버퍼 초기화
        receivedPayload = "";
    }
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

      setInterval(fetchGPSData, 1000); // 1초마다 GPS 데이터 갱신
    }

    function fetchGPSData() {
      fetch("/gps-data")
        .then(response => response.json())
        .then(data => {
          const lat = data.latitude;
          const lng = data.longitude;
          const address = data.address;
          const awsSpeed = data.aws_speed;

          if (lat !== 0 && lng !== 0) {
            const newPosition = { lat, lng };
            marker.setPosition(newPosition);
            map.setCenter(newPosition);
            document.getElementById("coordinates").innerText =
              `위도: ${lat}, 경도: ${lng}, AWS 속도: ${awsSpeed} km/h`;
            document.getElementById("address").innerText = `주소: ${address}`;
          } else {
            document.getElementById("coordinates").innerText =
              "GPS 데이터를 가져올 수 없습니다.";
            document.getElementById("address").innerText = "주소 없음";
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
  <p id="address">주소 로딩 중...</p>
</body>
</html>
)rawliteral");
}

// 웹 서버 JSON 데이터 제공
void handleGPSData() {
    processAWSData(); // AWS 데이터 업데이트
    String json = createGPSDataJSON(); // 최신 데이터 생성
    server.send(200, "application/json", json); // JSON 응답 반환
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
        if (awsIot.subscribe(SHADOW_UPDATE_DOCUMENTS_TOPIC, mySubCallBackHandler) == 0) {
            Serial.println("Shadow 업데이트 주제 구독 성공");
        } else {
            Serial.println("Shadow 업데이트 주제 구독 실패");
        }
    } else {
        Serial.println("AWS IoT 연결 실패");
    }

    server.on("/", handleRoot);
    server.on("/gps-data", handleGPSData);
    server.begin();
    Serial.println("HTTP 서버 시작");
}

void loop() {
    readGPS();
    processGPSData();
    processAWSData(); // AWS 데이터 처리
    server.handleClient();
}
