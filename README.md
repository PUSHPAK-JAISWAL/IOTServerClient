# IOTServerClient — README

Lightweight Arduino/ESP client library to sync variables with a self-hosted IoT server using a device API key.
This README explains how to use the library and — as requested — **how to implement adapters** (transport and storage) so the library can be reused with different protocols (HTTP, MQTT, WebSockets, etc.) or different local storage backends.

---

## Table of contents

* Overview
* Features
* Quick install
* Quick start (copy-paste)
* Server API contract
* Error handling & security notes
* Adapters — design and implementation (detailed)

  * Transport adapter interface
  * HTTP transport adapter (example)
  * MQTT transport adapter (example)
  * Storage adapter interface + LittleFS example
  * How to plug adapters into `IOTServerClient`
* Examples
* License

---

## Overview

`IOTServerClient` is an Arduino-style library for ESP32/ESP8266/Arduino that:

* Periodically sends heartbeat to server
* Pushes variables (device → server)
* Pulls variables (server → device) and updates local cache
* Supports typed variables and callback handlers for server-side changes
* Uses a long-lived `X-DEVICE-KEY` device key for authentication

The library is intentionally transport-agnostic through an adapter layer so you can switch between HTTP, MQTT, WebSockets, or any other transport without modifying core logic.

---

## Features

* Heartbeat (`/api/device/heartbeat`)
* Push variable (`/api/device/variable`)
* Pull variables (`/api/device/variables`)
* Local typed cache and typed getters
* Callback registration: `onWriteBool`, `onWriteInt`, `onWriteFloat`, `onWriteString`
* Adapter-based transport and storage (pluggable)

---

## Quick install

1. Create a folder `IOTServerClient/` and place `src/` and `examples/` as provided.
2. In Arduino IDE: `Sketch → Include Library → Add .ZIP Library` with the zipped project OR copy repo to Arduino libraries directory.
3. For PlatformIO, add Git URL or install as a local library.

---

## Quick start (copy-paste)

```cpp
#include <WiFi.h>
#include "IOTServerClient.h"

#define DEVICE_KEY "dvc_...your-key..."
#define SERVER_URL "http://your.server:8080"

IOTServerClient iot(DEVICE_KEY, SERVER_URL);

void onLed(bool v) { digitalWrite(2, v ? HIGH : LOW); }

void setup(){
  WiFi.begin("SSID","PASS");
  while (WiFi.status() != WL_CONNECTED) delay(200);
  pinMode(2, OUTPUT);
  iot.onWriteBool("led", onLed);
  iot.begin();
}

void loop(){
  iot.virtualWrite("temperature", 26.3f);
  iot.loop();
  delay(10);
}
```

---

## Server API contract (server must implement)

* **Headers**: `X-DEVICE-KEY: dvc_...`

1. `POST /api/device/heartbeat`
   Body: `{ "status": "online", "ts": 123456 }`
   Response: `{ "success": true }`
2. `GET /api/device/variables`
   Response:

   ```json
   {
     "variables": [
       { "name":"led", "type":"bool", "value":"true" },
       { "name":"setpoint", "type":"float", "value":"23.5" }
     ]
   }
   ```
3. `POST /api/device/variable`
   Body: `{ "name":"foo", "type":"int","value":"123" }`
   Response: `{ "success": true }`

---

## Error handling & security notes

* Always use `https://` and `WiFiClientSecure` in production.
* Keep device keys secret; store them in secure persistent storage.
* Server should support key rotation and revocation.
* Rate-limit device endpoints and validate payloads server-side.

---

# Adapters — design and implementation

The library core should only perform logic (heartbeat/sync/cache/dispatch). Network and storage operations are injected via adapters. Below is a concise, complete guide to design and implement adapters.

---

## Transport adapter — interface

Create a pure-virtual (abstract) interface `ITransportAdapter` that exposes the minimal operations the client needs:

```cpp
// ITransportAdapter.h
#ifndef ITRANSPORTADAPTER_H
#define ITRANSPORTADAPTER_H

#include <Arduino.h>

class ITransportAdapter {
public:
  virtual ~ITransportAdapter() {}
  // synchronous GET: returns response body or empty string
  virtual String get(const String& endpoint, const String& headers = "") = 0;
  // synchronous POST: returns response body or empty string
  virtual String post(const String& endpoint, const String& body, const String& headers = "") = 0;
  // called each loop (for adapters that need background handling)
  virtual void loop() {}
  // connect / initialize (optional)
  virtual bool connect() { return true; }
};

#endif
```

**Rationale:** keep it small and sync for simplicity; async adapters can buffer and expose `loop()`.

---

## HTTP transport adapter — example implementation

Use `HTTPClient` internally and the `X-DEVICE-KEY` header.

```cpp
// HttpAdapter.h
#ifndef HTTPADAPTER_H
#define HTTPADAPTER_H

#include "ITransportAdapter.h"
#include <HTTPClient.h>
#include <WiFiClient.h>

class HttpAdapter : public ITransportAdapter {
public:
  HttpAdapter(const String& baseUrl, const String& deviceKey)
    : base(baseUrl), key(deviceKey) { if (base.endsWith("/")) base.remove(base.length()-1); }

  bool connect() override {
    // nothing to do — ensure WiFi connected before use
    return WiFi.status() == WL_CONNECTED;
  }

  String get(const String& endpoint, const String& headers = "") override {
    WiFiClient wifiClient;
    HTTPClient http;
    String url = base + endpoint;
    if (!http.begin(wifiClient, url)) return "";
    http.addHeader("X-DEVICE-KEY", key);
    http.addHeader("Content-Type", "application/json");
    int code = http.GET();
    String payload = "";
    if (code >= 200 && code < 300) payload = http.getString();
    http.end();
    return payload;
  }

  String post(const String& endpoint, const String& body, const String& headers = "") override {
    WiFiClient wifiClient;
    HTTPClient http;
    String url = base + endpoint;
    if (!http.begin(wifiClient, url)) return "";
    http.addHeader("X-DEVICE-KEY", key);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(body);
    String payload = "";
    if (code >= 200 && code < 300) payload = http.getString();
    http.end();
    return payload;
  }

private:
  String base;
  String key;
};

#endif
```

**Usage with `IOTServerClient`:**

* Add a setter in `IOTServerClient`:

  ```cpp
  void setTransport(ITransportAdapter* adapter) { transport = adapter; }
  ```
* When making requests, call `transport->get("/api/device/variables")` instead of `HTTPClient` internals.

---

## MQTT transport adapter — outline & example

MQTT is publish/subscribe; we map endpoints:

* Device → server: publish to `devices/<deviceKey>/out` (or `variables/update`)
* Server → device: subscribe to `devices/<deviceKey>/in` (or `variables/set`)
* Heartbeat: publish to `devices/<deviceKey>/heartbeat`

Simplified adapter using `PubSubClient`:

```cpp
// MqttAdapter.h (concept)
#ifndef MQTTADAPTER_H
#define MQTTADAPTER_H

#include "ITransportAdapter.h"
#include <PubSubClient.h>
#include <WiFiClient.h>

class MqttAdapter : public ITransportAdapter {
public:
  MqttAdapter(WiFiClient& netClient, const char* broker, int port, const String& deviceKey)
    : client(netClient), pubsub(client), host(broker), port(port), key(deviceKey) {}

  bool connect() override {
    pubsub.setServer(host, port);
    pubsub.setCallback([this](char* topic, byte* payload, unsigned int length){
      // convert payload to String and store latest message; `loop()` will parse/dispatch
    });
    if (!pubsub.connected()) {
      if (!pubsub.connect(key.c_str())) return false;
    }
    pubsub.subscribe((String("devices/") + key + "/in").c_str());
    return true;
  }

  String get(const String& endpoint, const String& headers = "") override {
    // MQTT is not request-response; implement by returning last message for a "variables" topic
    // Keep a local buffer updated from callback, and return it here.
    return lastVarsPayload;
  }

  String post(const String& endpoint, const String& body, const String& headers = "") override {
    // map endpoint to topic
    String topic = "devices/" + key + "/out";
    pubsub.publish(topic.c_str(), body.c_str());
    return "{}";
  }

  void loop() override { pubsub.loop(); }

private:
  WiFiClient client;
  PubSubClient pubsub;
  const char* host;
  int port;
  String key;
  String lastVarsPayload;
};

#endif
```

**Notes:**

* The MQTT adapter makes `get()` return the latest cached `variables` payload (populated in the MQTT callback). This keeps the `IOTServerClient` core unchanged (calls `get("/api/device/variables")` and receives data).
* Implement JSON payload format identical to HTTP to reuse parsing logic in `IOTServerClient`.

---

## Storage adapter — interface & LittleFS example

Persist deviceKey and cache to retain across reboots.

```cpp
// IStorageAdapter.h
#ifndef ISTORAGEADAPTER_H
#define ISTORAGEADAPTER_H

#include <Arduino.h>

class IStorageAdapter {
public:
  virtual ~IStorageAdapter() {}
  virtual bool begin() = 0;
  virtual bool saveString(const String& key, const String& value) = 0;
  virtual String readString(const String& key) = 0;
  virtual bool exists(const String& key) = 0;
};

#endif
```

**LittleFS implementation (ESP32/ESP8266):**

```cpp
// LittleFsStorage.h
#include "IStorageAdapter.h"
#include <LittleFS.h>

class LittleFsStorage : public IStorageAdapter {
public:
  bool begin() override { return LittleFS.begin(); }
  bool saveString(const String& key, const String& value) override {
    File f = LittleFS.open("/" + key, "w");
    if (!f) return false;
    f.print(value);
    f.close();
    return true;
  }
  String readString(const String& key) override {
    if (!LittleFS.exists("/" + key)) return "";
    File f = LittleFS.open("/" + key, "r");
    if (!f) return "";
    String s = f.readString();
    f.close();
    return s;
  }
  bool exists(const String& key) override { return LittleFS.exists("/" + key); }
};
```

**How to use:**

* `IOTServerClient` gets a `IStorageAdapter*` via `setStorage(...)`.
* On `begin()`, `IOTServerClient` can `storage->readString("deviceKey")` (or save cache periodically).

---

## How to plug adapters into `IOTServerClient`

1. Add adapter pointers in the class:

```cpp
ITransportAdapter* transport = nullptr;
IStorageAdapter* storage = nullptr;

void setTransport(ITransportAdapter* t) { transport = t; if (transport) transport->connect(); }
void setStorage(IStorageAdapter* s) { storage = s; if (storage) storage->begin(); }
```

2. Replace internal network calls with `transport->get(...)`/`post(...)`. Call `transport->loop()` inside `IOTServerClient::loop()`.
3. Use `storage` to persist deviceKey or cached variables:

```cpp
if (storage && storage->exists("deviceKey")) {
  deviceKey = storage->readString("deviceKey");
}
```

4. For MQTT adapters, ensure `transport->loop()` is called frequently.

**Example initialization in sketch:**

```cpp
#include "HttpAdapter.h"
#include "LittleFsStorage.h"

HttpAdapter http(SERVER_URL, DEVICE_KEY);
LittleFsStorage fs;

IOTServerClient iot(DEVICE_KEY, SERVER_URL);
void setup(){
  LittleFS.begin();
  iot.setTransport(&http);
  iot.setStorage(&fs);
  iot.begin();
}
```

---

## Examples

* See `examples/ESP32_example/ESP32_example.ino` and `examples/ESP8266_example/ESP8266_example.ino` for simple HTTP usage.
* For MQTT: implement `MqttAdapter` and use `iot.setTransport(&mqttAdapter);`

---

