#include <WiFi.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <WebServer.h>

// Wi-Fi 설정
const char* ssid = "KIMBO";       // Wi-Fi 이름
const char* password = "03011777"; // Wi-Fi 비밀번호

// GPS 객체 및 UART 설정
TinyGPSPlus gps;
HardwareSerial SerialGPS(1); // UART1 사용 (RX=GPIO16, TX=GPIO17)

// 웹 서버
WebServer server(80);

// 주기적 업데이트를 위한 변수
unsigned long lastUpdateTime = 0; // 마지막 업데이트 시간
const unsigned long updateInterval = 2000; // 업데이트 간격 (2초)

// GPS 데이터를 JSON 형식으로 생성
String createGPSDataJSON() {
  String jsonData = "{";
  if (gps.location.isValid()) {
    jsonData += "\"latitude\":" + String(gps.location.lat(), 6) + ",";
    jsonData += "\"longitude\":" + String(gps.location.lng(), 6) + ",";
    jsonData += "\"satellites\":" + String(gps.satellites.value()) + ",";
    jsonData += "\"hdop\":" + String(gps.hdop.value());
  } else {
    jsonData += "\"latitude\":0,\"longitude\":0,\"satellites\":0,\"hdop\":0";
  }
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
  unsigned long currentTime = millis(); // 현재 시간
  if (currentTime - lastUpdateTime >= updateInterval) {
    lastUpdateTime = currentTime; // 마지막 업데이트 시간 갱신

    // 위성 개수 출력
    if (gps.satellites.isValid()) {
      Serial.print("위성 개수: ");
      Serial.println(gps.satellites.value());
    } else {
      Serial.println("위성을 찾는 중...");
    }

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


// HTML 페이지 제공
void handleRoot() {
  server.send(200, "text/html; charset=utf-8", R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32 GPS Tracker</title>
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

      setInterval(fetchGPSData, 2000); // 2초마다 GPS 데이터 갱신
    }

    function fetchGPSData() {
      fetch("/gps-data")
        .then(response => response.json())
        .then(data => {
          const lat = data.latitude;
          const lng = data.longitude;
          const satellites = data.satellites;

          if (lat !== 0 && lng !== 0) {
            const newPosition = { lat, lng };
            marker.setPosition(newPosition);
            map.setCenter(newPosition);
            document.getElementById("coordinates").innerText =
              `위도: ${lat}, 경도: ${lng}, 위성 갯수: ${satellites}`;
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
  <h1>ESP32 GPS Tracker</h1>
  <div id="map" style="width: 90%; height: 500px;"></div>
  <p id="coordinates">GPS 데이터 로딩중...</p>
</body>
</html>
)rawliteral");
}

// GPS 데이터를 JSON 형식으로 제공
void handleGPSData() {
  String json = createGPSDataJSON();
  server.send(200, "application/json", json);
}

// 잘못된 요청 처리
void handleNotFound() {
  server.send(404, "text/plain", "404: Not Found");
}

// 초기화
void setup() {
  Serial.begin(115200);
  SerialGPS.begin(9600, SERIAL_8N1, 16, 17); // RX=GPIO16, TX=GPIO17
  // GPS 핫 스타트 (빠른 초기화)
  SerialGPS.println("$PMTK314,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*29"); // GGA 메시지만 요청
  SerialGPS.println("$PMTK220,1000*1F"); // 업데이트 속도: 1Hz
  Serial.println("GPS 초기화 명령 전송 완료");

  Serial.println("GPS 초기화 완료");

  // Wi-Fi 연결
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Wi-Fi 연결 시도 중...");
  }
  Serial.println("Wi-Fi 연결 완료!");
  Serial.print("ESP32 IP 주소: ");
  Serial.println(WiFi.localIP());

  // 웹 서버 경로 설정
  server.on("/", handleRoot);
  server.on("/gps-data", handleGPSData);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP 서버 시작");
}

// 메인 루프
void loop() {
  readGPS();         // GPS 데이터 읽기
  processGPSData();  // 2초마다 GPS 데이터 처리
  server.handleClient(); // 웹 서버 요청 처리
}
