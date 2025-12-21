#include "IOTServerClient.h"

IOTServerClient::IOTServerClient(const String& key, const String& url)
: deviceKey(key), serverUrl(url) {
    if (serverUrl.endsWith("/")) serverUrl.remove(serverUrl.length() - 1);
}

bool IOTServerClient::begin() {
    lastHeartbeat = millis();
    // library doesn't manage WiFi; caller must connect
    return true;
}

bool IOTServerClient::isConnected() {
    return (WiFi.status() == WL_CONNECTED);
}

void IOTServerClient::setHeartbeatInterval(unsigned long ms) {
    heartbeatInterval = ms;
}

void IOTServerClient::loop() {
    unsigned long now = millis();
    if (now - lastHeartbeat >= heartbeatInterval) {
        sendHeartbeat();
        syncNow();
        lastHeartbeat = now;
    }
}

String IOTServerClient::varTypeToString(VarType t) {
    switch (t) {
        case INT_TYPE: return "int";
        case FLOAT_TYPE: return "float";
        case BOOLEAN_TYPE: return "bool";
        case STRING_TYPE: return "string";
        default: return "string";
    }
}

VarType IOTServerClient::stringToVarType(const String& s) {
    if (s.equalsIgnoreCase("int")) return INT_TYPE;
    if (s.equalsIgnoreCase("float")) return FLOAT_TYPE;
    if (s.equalsIgnoreCase("bool") || s.equalsIgnoreCase("boolean")) return BOOLEAN_TYPE;
    return STRING_TYPE;
}

String IOTServerClient::makeRequest(const String& endpoint, const String& method, const String& payload, int retries) {
    if (!isConnected()) return "";

    String url = serverUrl + endpoint;
    int attempts = 0;
    while (attempts <= retries) {
        if (!http.begin(wifiClient, url)) {
            attempts++;
            delay(100);
            continue;
        }

        // headers
        http.addHeader("Content-Type", "application/json");
        http.addHeader("X-DEVICE-KEY", deviceKey);

        int httpCode = 0;
        if (method == "GET") {
            httpCode = http.GET();
        } else if (method == "POST") {
            httpCode = http.POST(payload);
        } else if (method == "PUT") {
            httpCode = http.PUT(payload);
        } else {
            // fallback
            httpCode = http.sendRequest(method.c_str(), payload.c_str());
        }

        String response = "";
        if (httpCode >= 200 && httpCode < 300) {
            response = http.getString();
        }
        http.end();

        if (response.length() > 0) return response;
        attempts++;
        // small backoff (non-blocking in real design; light blocking here)
        delay(100 * attempts);
    }
    return "";
}

bool IOTServerClient::sendHeartbeat() {
    StaticJsonDocument<128> doc;
    doc["status"] = "online";
    doc["ts"] = millis();

    String payload;
    serializeJson(doc, payload);
    String res = makeRequest("/api/device/heartbeat", "POST", payload, 1);
    if (res.length() == 0) return false;

    // optional: parse response for success
    DynamicJsonDocument rd(256);
    if (deserializeJson(rd, res)) return false;
    if (rd.containsKey("success")) return rd["success"] | false;
    return true;
}

bool IOTServerClient::sendVariable(const String& name, const String& value, VarType type) {
    StaticJsonDocument<256> doc;
    doc["name"] = name;
    doc["value"] = value;
    doc["type"] = varTypeToString(type);

    String payload;
    serializeJson(doc, payload);

    String res = makeRequest("/api/device/variable", "POST", payload, 1);
    if (res.length() == 0) return false;

    // parse success optionally
    DynamicJsonDocument rd(256);
    if (!deserializeJson(rd, res)) return true; // if response not parseable, assume success
    if (rd.containsKey("success")) return rd["success"] | true;
    return true;
}

bool IOTServerClient::virtualWrite(const String& name, int value) {
    String val = String(value);
    bool ok = sendVariable(name, val, INT_TYPE);
    if (ok) updateCache(name, val, INT_TYPE);
    return ok;
}

bool IOTServerClient::virtualWrite(const String& name, float value) {
    String val = String(value, 6);
    bool ok = sendVariable(name, val, FLOAT_TYPE);
    if (ok) updateCache(name, val, FLOAT_TYPE);
    return ok;
}

bool IOTServerClient::virtualWrite(const String& name, bool value) {
    String val = value ? "true" : "false";
    bool ok = sendVariable(name, val, BOOLEAN_TYPE);
    if (ok) updateCache(name, val, BOOLEAN_TYPE);
    return ok;
}

bool IOTServerClient::virtualWrite(const String& name, const String& value) {
    bool ok = sendVariable(name, value, STRING_TYPE);
    if (ok) updateCache(name, value, STRING_TYPE);
    return ok;
}

Variable* IOTServerClient::findInCache(const String& name) {
    for (auto &v : cache) {
        if (v.name == name) return &v;
    }
    return nullptr;
}

void IOTServerClient::updateCache(const String& name, const String& value, VarType type) {
    Variable* v = findInCache(name);
    if (v) {
        v->value = value;
        v->type = type;
    } else {
        Variable nv;
        nv.name = name;
        nv.type = type;
        nv.value = value;
        cache.push_back(nv);
    }
    // call any registered callbacks for this name/type
    processUpdate(name, value, type);
}

void IOTServerClient::processUpdate(const String& name, const String& value, VarType type) {
    for (auto &cb : callbacks) {
        if (cb.name == name) {
            // match type and call if exists
            if (type == INT_TYPE && cb.intCb) {
                cb.intCb(value.toInt());
            } else if (type == FLOAT_TYPE && cb.floatCb) {
                cb.floatCb(value.toFloat());
            } else if (type == BOOLEAN_TYPE && cb.boolCb) {
                String vv = value;
                bool b = (vv.equalsIgnoreCase("true") || vv == "1");
                cb.boolCb(b);
            } else if (type == STRING_TYPE && cb.stringCb) {
                cb.stringCb(value);
            }
        }
    }
}

bool IOTServerClient::syncNow() {
    String res = makeRequest("/api/device/variables", "GET", "", 1);
    if (res.length() == 0) return false;

    // parse { variables: [ { name, type, value }, ... ] }
    DynamicJsonDocument d(4096);
    DeserializationError err = deserializeJson(d, res);
    if (err) return false;

    if (!d.containsKey("variables")) return false;
    JsonArray arr = d["variables"].as<JsonArray>();
    for (JsonObject obj : arr) {
        String name = obj["name"].as<String>();
        String typeS = obj["type"].as<String>();
        String val = obj["value"].as<String>();
        VarType vt = stringToVarType(typeS);
        updateCache(name, val, vt);
    }
    return true;
}

// Read methods
int IOTServerClient::virtualReadInt(const String& name) {
    Variable* v = findInCache(name);
    if (!v) return 0;
    return v->value.toInt();
}
float IOTServerClient::virtualReadFloat(const String& name) {
    Variable* v = findInCache(name);
    if (!v) return 0.0f;
    return v->value.toFloat();
}
bool IOTServerClient::virtualReadBool(const String& name) {
    Variable* v = findInCache(name);
    if (!v) return false;
    String vv = v->value;
    return (vv.equalsIgnoreCase("true") || vv == "1");
}
String IOTServerClient::virtualReadString(const String& name) {
    Variable* v = findInCache(name);
    if (!v) return String("");
    return v->value;
}

// Callback registration
void IOTServerClient::onWriteInt(const String& name, IntCallback cb) {
    for (auto &e : callbacks) if (e.name == name) { e.intCb = cb; return; }
    CallbackEntry e; e.name = name; e.type = INT_TYPE; e.intCb = cb; callbacks.push_back(e);
}
void IOTServerClient::onWriteFloat(const String& name, FloatCallback cb) {
    for (auto &e : callbacks) if (e.name == name) { e.floatCb = cb; return; }
    CallbackEntry e; e.name = name; e.type = FLOAT_TYPE; e.floatCb = cb; callbacks.push_back(e);
}
void IOTServerClient::onWriteBool(const String& name, BoolCallback cb) {
    for (auto &e : callbacks) if (e.name == name) { e.boolCb = cb; return; }
    CallbackEntry e; e.name = name; e.type = BOOLEAN_TYPE; e.boolCb = cb; callbacks.push_back(e);
}
void IOTServerClient::onWriteString(const String& name, StringCallback cb) {
    for (auto &e : callbacks) if (e.name == name) { e.stringCb = cb; return; }
    CallbackEntry e; e.name = name; e.type = STRING_TYPE; e.stringCb = cb; callbacks.push_back(e);
}
