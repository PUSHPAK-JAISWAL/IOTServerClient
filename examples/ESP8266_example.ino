#include <ESP8266WiFi.h>
#include "IOTServerClient.h"

const char* ssid = "YOUR_SSID";
const char* pass = "YOUR_PASS";

#define DEVICE_KEY "dvc_XXXXXXXXXXXXXXXXXXXX"
#define SERVER_URL "http://192.168.1.100:8080"

IOTServerClient iot(DEVICE_KEY, SERVER_URL);

const int LED_PIN = LED_BUILTIN; // on many boards

void onLedWrite(bool v) {
  digitalWrite(LED_PIN, v ? LOW : HIGH); // builtin LED is inverted on some boards
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) delay(200);

  iot.onWriteBool("led", onLedWrite);
  iot.begin();
  iot.setHeartbeatInterval(30000UL);
}

void loop() {
  static unsigned long last = 0;
  if (millis() - last > 15000) {
    last = millis();
    int v = analogRead(A0) / 4;
    iot.virtualWrite("light", v);
  }
  iot.loop();
  delay(10);
}
