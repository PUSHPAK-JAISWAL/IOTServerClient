#ifndef IOT_SERVER_CLIENT_H
#define IOT_SERVER_CLIENT_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <vector>
#include <functional>

// public variable types
enum VarType { INT_TYPE, FLOAT_TYPE, STRING_TYPE, BOOLEAN_TYPE };

struct Variable {
    String name;
    VarType type;
    String value;
};

typedef std::function<void(const String&)> StringCallback;
typedef std::function<void(int)> IntCallback;
typedef std::function<void(float)> FloatCallback;
typedef std::function<void(bool)> BoolCallback;

class IOTServerClient {
public:
    // deviceKey: issued by server (long-lived API key)
    // serverUrl: base url e.g. "http://192.168.1.10:8080"
    IOTServerClient(const String& deviceKey, const String& serverUrl);

    // Must call after WiFi.begin and connecting to WiFi
    bool begin();

    // call regularly from loop()
    void loop();

    // set heartbeat interval (ms)
    void setHeartbeatInterval(unsigned long ms);

    // write variables (sends to server and updates local cache)
    bool virtualWrite(const String& name, int value);
    bool virtualWrite(const String& name, float value);
    bool virtualWrite(const String& name, bool value);
    bool virtualWrite(const String& name, const String& value);

    // read cached variable (no network). returns 0 / "" / false if not found
    int virtualReadInt(const String& name);
    float virtualReadFloat(const String& name);
    bool virtualReadBool(const String& name);
    String virtualReadString(const String& name);

    // register callbacks (device reacts when server-side value changes)
    void onWriteInt(const String& name, IntCallback cb);
    void onWriteFloat(const String& name, FloatCallback cb);
    void onWriteBool(const String& name, BoolCallback cb);
    void onWriteString(const String& name, StringCallback cb);

    // manual sync
    bool syncNow();

    // helper
    bool isConnected();

private:
    String deviceKey;
    String serverUrl;

    WiFiClient wifiClient;
    HTTPClient http;

    unsigned long lastHeartbeat = 0;
    unsigned long heartbeatInterval = 30000UL; // default 30s

    std::vector<Variable> cache;

    struct CallbackEntry {
        String name;
        VarType type;
        IntCallback intCb;
        FloatCallback floatCb;
        BoolCallback boolCb;
        StringCallback stringCb;
    };
    std::vector<CallbackEntry> callbacks;

    // low-level
    String makeRequest(const String& endpoint, const String& method, const String& payload = "", int retries = 1);
    String varTypeToString(VarType t);
    VarType stringToVarType(const String& s);
    void processUpdate(const String& name, const String& value, VarType type);
    bool sendHeartbeat();
    bool sendVariable(const String& name, const String& value, VarType type);
    void updateCache(const String& name, const String& value, VarType type);
    Variable* findInCache(const String& name);
};

#endif
