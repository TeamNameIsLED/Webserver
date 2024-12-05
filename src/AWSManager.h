#ifndef AWS_MANAGER_H
#define AWS_MANAGER_H

#include <AWS_IOT.h>
#include <Arduino_JSON.h>


// AWS IoT 설정
AWS_IOT awsIot;
char HOST_ADDRESS[] = "a3pecuomf1y0cd-ats.iot.ap-northeast-2.amazonaws.com";
char CLIENT_ID[] = "WebServerDevicea";
char SHADOW_UPDATE_DOCUMENTS_TOPIC[] = "$aws/things/ESP32_BIKEASSIST/shadow/update/accepted";
char TOPIC_ALERTS[] = "esp32/alerts";
char TOPIC_TELEMETRY[] = "esp32/telemetry";

float awsSpeed = 0.0;
String status = "";
String latestAlert = "";
unsigned long buzzerStartTime = 0;
bool isBuzzerOn = false;

void setupAWS() {
    if (awsIot.connect(HOST_ADDRESS, CLIENT_ID) == 0) {
        Serial.println("AWS IoT 연결 성공");
        awsIot.subscribe(SHADOW_UPDATE_DOCUMENTS_TOPIC, mySubCallBackHandler);
        awsIot.subscribe(TOPIC_ALERTS, mySubCallBackHandler);
    } else {
        Serial.println("AWS IoT 연결 실패");
    }
}

// AWS 메시지 콜백 핸들러
void mySubCallBackHandler(char* topicName, int payloadLen, char* payLoad) {
    String receivedPayload = String(payLoad).substring(0, payloadLen);
    JSONVar parsed = JSON.parse(receivedPayload);

    if (JSON.typeof(parsed) == "undefined") {
        Serial.println("AWS IoT 메시지 파싱 실패");
        return;
    }

    if (parsed.hasOwnProperty("state") && parsed["state"].hasOwnProperty("reported")) {
        JSONVar reported = parsed["state"]["reported"];
        if (reported.hasOwnProperty("speed")) {
            awsSpeed = (float)((double)reported["speed"]);
        }
        if (reported.hasOwnProperty("status")) {
            status = (const char*)reported["status"];
        }
    }

    if (parsed.hasOwnProperty("alert") && parsed.hasOwnProperty("description")) {
        String alertMessage = (const char*)parsed["alert"];
        if (alertMessage == "주의: 이 지역은 사고 위험이 높은 구간입니다!" && !isBuzzerOn) {
            digitalWrite(23, HIGH);
            buzzerStartTime = millis();
            isBuzzerOn = true;
        }
        JSONVar alertJson;
        alertJson["alert"] = alertMessage;
        alertJson["description"] = (const char*)parsed["description"];
        latestAlert = JSON.stringify(alertJson);
    }
}

void processAlerts() {
    if (isBuzzerOn && millis() - buzzerStartTime > 3000) {
        digitalWrite(23, LOW);
        isBuzzerOn = false;
    }
}

void sendTelemetry(String address) {
    if (!address.isEmpty()) {
        char payload[256];
        snprintf(payload, sizeof(payload), "{\"address\": \"%s\", \"speed\": %.2f, \"status\": \"%s\"}",
                 address.c_str(), awsSpeed, status.c_str());
        awsIot.publish(TOPIC_TELEMETRY, payload);
    }
}


#endif
