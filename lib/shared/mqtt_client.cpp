// mqtt_client.cpp -- pure MQTT transport
#include "mqtt_client.h"
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include "app_state.h"
#include "app_config.h"
#include "util.h"

static MqttClientDisplay    sDisplay;
static MqttMessageHandler   sMessageHandler   = nullptr;
static MqttConnectedHandler sConnectedHandler = nullptr;

void setMqttClientDisplayCallbacks(const MqttClientDisplay& cb) { sDisplay = cb; }
void setMqttMessageHandler(MqttMessageHandler handler)          { sMessageHandler   = handler; }
void setMqttConnectedHandler(MqttConnectedHandler handler)      { sConnectedHandler = handler; }

static void _kickSpinner(unsigned long durationMs = 1500) {
  if (sDisplay.kickSpinner) sDisplay.kickSpinner(durationMs);
}
static void _setStatus(const String& msg, unsigned long holdMs = 3000) {
  if (sDisplay.setStatus) sDisplay.setStatus(msg, holdMs);
}

static bool serializeDocToBuffer(const JsonDocument& doc, char* buf, size_t sz, size_t& outLen) {
  outLen = 0;
  if (!buf || sz == 0) return false;
  size_t needed = measureJson(doc);
  if (needed >= sz) {
    Serial.print("[MQTT] payload too large, need="); Serial.print(needed);
    Serial.print(" buf="); Serial.println(sz);
    return false;
  }
  outLen = serializeJson(doc, buf, sz);
  if (outLen == 0) { Serial.println("[MQTT] serializeJson empty"); return false; }
  return true;
}

bool publishJsonDocToTopic(const char* topic, const JsonDocument& doc, bool retained) {
  if (!mqtt.connected() || !topic || !topic[0]) return false;
  char buffer[mqttbuffersize];
  size_t n = 0;
  if (!serializeDocToBuffer(doc, buffer, sizeof(buffer), n)) {
    Serial.print("[MQTT] publish skipped: "); Serial.println(topic);
    return false;
  }
  bool ok = mqtt.publish(topic, reinterpret_cast<const uint8_t*>(buffer), n, retained);
  if (ok) { mqttPublishCount++; _kickSpinner(); }
  else { Serial.print("[MQTT] publish failed: "); Serial.println(topic); }
  return ok;
}

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  msg.reserve(length);
  for (unsigned int i = 0; i < length; i++) msg += char(payload[i]);
  lastRxRaw = msg;
  if (sMessageHandler) sMessageHandler(String(topic), msg);
  else lastRxType = "unhandled";
  _kickSpinner();
}

bool mqttConnect() {
  mqtt.setServer(config.mqttHost, config.mqttPort);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(mqttbuffersize);

  String clientId = "temp-network-" + sanitizeTopicPart(safeDeviceId()) + "-" + String(ESP.getChipId(), HEX);

  StaticJsonDocument<160> willDoc;
  willDoc["type"]   = "status";
  willDoc["id"]     = safeDeviceId();
  willDoc["online"] = false;
  char willPayload[160];
  size_t willLen = 0;
  if (!serializeDocToBuffer(willDoc, willPayload, sizeof(willPayload), willLen)) {
    _setStatus("will too large", 2000);
    return false;
  }

  _setStatus("connecting brkr", 2000);
  Serial.println("[MQTT] connect attempt");
  Serial.print("[MQTT] host: ");   Serial.println(config.mqttHost);
  Serial.print("[MQTT] port: ");   Serial.println(config.mqttPort);
  Serial.print("[MQTT] client: "); Serial.println(clientId);

  bool ok = mqtt.connect(clientId.c_str(), nullptr, nullptr, statusTopic, 0, true, willPayload);
  Serial.print("[MQTT] result: "); Serial.println(ok ? "ok" : "fail");
  Serial.print("[MQTT] state: ");  Serial.println(mqtt.state());

  if (!ok) { _setStatus("broker conn fail", 2000); return false; }

  bool subOk = mqtt.subscribe(commandTopic);
  Serial.print("[MQTT] subscribe: "); Serial.println(subOk ? "ok" : "failed");
  if (!subOk) { _setStatus("cmd sub failed", 2000); mqtt.disconnect(); return false; }

  _setStatus("broker connected", 2000);

  // Notify sketch -- fires initialSampleAndPublish equivalent with real data
  if (sConnectedHandler) sConnectedHandler();

  return true;
}

void initMqttClient() {
  mqtt.setServer(config.mqttHost, config.mqttPort);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(mqttbuffersize);
}

void startMqttIfWifiReady() {
  if (WiFi.status() == WL_CONNECTED)
    mqtt.setServer(config.mqttHost, config.mqttPort);
}

void serviceMqttClient() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!mqtt.connected()) {
    mqttOnlinePublished = false;
    if (millis() - lastMqttAttemptMs >= mqttretryms) {
      lastMqttAttemptMs = millis();
      mqttConnect();
    }
    return;
  }
  mqtt.loop();
}
