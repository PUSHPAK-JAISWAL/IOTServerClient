#include <WiFi.h>
#include "IOTServerClient.h"

const char* ssid = "YOUR_SSID";
const char* pass = "YOUR_PASS";

#define DEVICE_KEY "dvc_XXXXXXXXXXXXXXXXXXXX"
#define SERVER_URL "http://192.168.1.100:8080"

IOTServerClient iot(DEVICE_KEY, SERVER_URL);

const int LED_PIN = 2;

void onLedWrite(bool v) {
  digitalWrite(LED_PIN, v ? HIGH : LOW);
  Serial.printf("LED set to %d\n", v);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  WiFi.begin(ssid, pass);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println(" connected");

  iot.onWriteBool("led", onLedWrite);
  iot.begin();
  iot.setHeartbeatInterval(30000UL);
}

void loop() {
  // simulate temperature reading
  static unsigned long last = 0;
  if (millis() - last > 10000) {
    last = millis();
    float temp = random(200, 300) / 10.0;
    iot.virtualWrite("temperature", temp);
    Serial.printf("sent temperature: %.2f\n", temp);
  }

  iot.loop();
  delay(10);
}
