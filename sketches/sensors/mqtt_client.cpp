#include "mqtt_client.h"
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include "app_state.h"
#include "app_config.h"
#include "sensor_names.h"
#include "water_probe.h"
#include "sensor_bus.h"
#include "util.h"
#include "display_ui.h"
#include "mqtt_commands.h"

static bool serializeDocToBuffer(const JsonDocument& doc, char* buffer, size_t bufferSize, size_t& outLen) {
  outLen = 0;
  if (!buffer || bufferSize == 0) return false;

  size_t needed = measureJson(doc);
  if (needed >= bufferSize) {
    Serial.print("[MQTT] payload too large, need bytes=");
    Serial.print(needed);
    Serial.print(" buffer=");
    Serial.println(bufferSize);
    return false;
  }

  outLen = serializeJson(doc, buffer, bufferSize);
  if (outLen == 0) {
    Serial.println("[MQTT] serializeJson produced empty payload");
    return false;
  }
  return true;
}

bool publishJsonDocToTopic(const char* topic, const JsonDocument& doc, bool retained) {
  if (!mqtt.connected()) return false;
  if (!topic || !topic[0]) return false;

  char buffer[mqttbuffersize];
  size_t n = 0;
  if (!serializeDocToBuffer(doc, buffer, sizeof(buffer), n)) {
    Serial.print("[MQTT] publish skipped for topic: ");
    Serial.println(topic);
    return false;
  }

  bool ok = mqtt.publish(topic, reinterpret_cast<const uint8_t*>(buffer), n, retained);
  if (ok) {
    mqttPublishCount++;
    kickActivitySpinner();
  } else {
    Serial.print("[MQTT] publish failed for topic: ");
    Serial.println(topic);
  }
  return ok;
}

void publishAggregateStatus(bool retained) {
  Serial.print("heartbeat: mqtt connected=");
  Serial.println(mqtt.connected());
  if (!mqtt.connected()) return;

  StaticJsonDocument<1536> doc;
  doc["type"] = "status";
  doc["id"] = safeDeviceId();
  doc["online"] = true;
  doc["ssid"] = WiFi.SSID();
  doc["rssidbm"] = WiFi.RSSI();
  doc["ip"] = ipToString(WiFi.localIP());
  doc["freeheap"] = ESP.getFreeHeap();
  doc["sensorcount"] = sensorCount;
  doc["simulated"] = useFakeSensors;
  doc["networkdetected"] = sensorNetworkDetected;
  doc["prometheusport"] = config.prometheusPort;

  String ts = currentTimestampString();
  if (ts.length()) doc["timestamp"] = ts;

  appendWaterToJson(doc);

  JsonArray sensors = doc.createNestedArray("sensors");
  for (uint8_t i = 0; i < sensorCount; i++) {
    JsonObject s = sensors.createNestedObject();
    s["index"] = i + 1;
    s["name"] = sensorNames[i];
    s["address"] = sensorAddressString(i);
    s["connected"] = sensorPresent[i];
    if (!isnan(sensorTempsC[i])) {
      s["tempc"] = sensorTempsC[i];
      s["tempf"] = sensorTempsC[i] * 9.0f / 5.0f + 32.0f;
    }
  }

  if (publishJsonDocToTopic(statusTopic, doc, retained)) {
    setStatusMessage("publishing agg", 1500);
  }
}

void publishPerSensorStatus(uint8_t i, bool retained) {
  if (!mqtt.connected() || i >= sensorCount) return;

  StaticJsonDocument<512> doc;
  doc["type"] = "sensor";
  doc["id"] = safeDeviceId();
  doc["online"] = true;
  doc["index"] = i + 1;
  doc["name"] = sensorNames[i];
  doc["address"] = sensorAddressString(i);
  doc["connected"] = sensorPresent[i];
  doc["simulated"] = useFakeSensors;

  String ts = currentTimestampString();
  if (ts.length()) doc["timestamp"] = ts;

  if (!isnan(sensorTempsC[i])) {
    doc["tempc"] = sensorTempsC[i];
    doc["tempf"] = sensorTempsC[i] * 9.0f / 5.0f + 32.0f;
  }

  String topic = String(config.baseTopic) + "/" +
                 sanitizeTopicPart(safeDeviceId()) + "/sensor/" +
                 sanitizeTopicPart(sensorNames[i]);

  if (!publishJsonDocToTopic(topic.c_str(), doc, retained)) {
    Serial.print("[MQTT] per-sensor publish failed index=");
    Serial.println(i + 1);
  }
}

void publishPerSensorStatuses(bool retained) {
  for (uint8_t i = 0; i < sensorCount; i++) {
    publishPerSensorStatus(i, retained);
  }
}

void publishWaterStatus(bool retained) {
  if (!mqtt.connected()) return;

  StaticJsonDocument<512> doc;
  doc["type"] = "water";
  doc["id"] = safeDeviceId();
  doc["online"] = true;

  String ts = currentTimestampString();
  if (ts.length()) doc["timestamp"] = ts;

  appendWaterToJson(doc);

  if (!publishJsonDocToTopic(waterTopic, doc, retained)) {
    Serial.println("[MQTT] water publish failed");
  }
}

void publishCommandResult(const char* type, bool ok, const char* msg) {
  if (!mqtt.connected()) return;

  StaticJsonDocument<256> reply;
  reply["type"] = type ? type : "result";
  reply["id"] = safeDeviceId();
  reply["ok"] = ok;
  reply["message"] = msg ? msg : "";

  if (!publishJsonDocToTopic(resultsTopic, reply, false)) {
    Serial.println("[MQTT] command result publish failed");
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  msg.reserve(length);
  for (unsigned int i = 0; i < length; i++) {
    msg += char(payload[i]);
  }

  lastRxRaw = msg;

  if (String(topic) == commandTopic) {
    handleCommandJson(msg);
  } else {
    lastRxType = "other";
  }

  kickActivitySpinner();
}

bool mqttConnect() {
  mqtt.setServer(config.mqttHost, config.mqttPort);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(mqttbuffersize);

  String clientId = "temp-network-" + sanitizeTopicPart(safeDeviceId()) + "-" + String(ESP.getChipId(), HEX);

  StaticJsonDocument<160> willDoc;
  willDoc["type"] = "status";
  willDoc["id"] = safeDeviceId();
  willDoc["online"] = false;

  char willPayload[160];
  size_t willLen = 0;
  if (!serializeDocToBuffer(willDoc, willPayload, sizeof(willPayload), willLen)) {
    setStatusMessage("will too large", 2000);
    return false;
  }

  setStatusMessage("connecting brkr", 2000);
  Serial.println();
  Serial.println("[MQTT] connect attempt");
  Serial.print("[MQTT] host: ");
  Serial.println(config.mqttHost);
  Serial.print("[MQTT] port: ");
  Serial.println(config.mqttPort);
  Serial.print("[MQTT] clientId: ");
  Serial.println(clientId);
  Serial.print("[MQTT] status topic: ");
  Serial.println(statusTopic);
  Serial.print("[MQTT] command topic: ");
  Serial.println(commandTopic);
  Serial.print("[MQTT] wifi connected: ");
  Serial.println(WiFi.status() == WL_CONNECTED ? "yes" : "no");
  Serial.print("[MQTT] local IP: ");
  Serial.println(ipToString(WiFi.localIP()));
  Serial.print("[MQTT] RSSI: ");
  Serial.println(WiFi.RSSI());
  Serial.print("[MQTT] free heap before connect: ");
  Serial.println(ESP.getFreeHeap());

  bool ok = mqtt.connect(clientId.c_str(), nullptr, nullptr, statusTopic, 0, true, willPayload);

  Serial.print("[MQTT] connect result: ");
  Serial.println(ok ? "success" : "failure");
  Serial.print("[MQTT] mqtt.state(): ");
  Serial.println(mqtt.state());
  Serial.print("[MQTT] free heap after connect: ");
  Serial.println(ESP.getFreeHeap());

  if (!ok) {
    setStatusMessage("broker conn fail", 2000);
    return false;
  }

  bool subOk = mqtt.subscribe(commandTopic);
  Serial.print("[MQTT] subscribe command topic: ");
  Serial.println(subOk ? "ok" : "failed");

  if (!subOk) {
    setStatusMessage("cmd sub failed", 2000);
    mqtt.disconnect();
    return false;
  }

  publishAggregateStatus(true);
  publishPerSensorStatuses(true);
  publishWaterStatus(true);
  mqttOnlinePublished = true;

  setStatusMessage("broker connected", 2000);
  return true;
}

void initMqttClient() {
  mqtt.setServer(config.mqttHost, config.mqttPort);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(mqttbuffersize);
}

void startMqttIfWifiReady() {
  if (WiFi.status() == WL_CONNECTED) {
    mqtt.setServer(config.mqttHost, config.mqttPort);
  }
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

void initialSampleAndPublish() {
  scanSensors(true);
  readTemperatures();
  sampleWaterLevel();
  publishAggregateStatus(true);
  publishPerSensorStatuses(true);
  publishWaterStatus(true);
  mqttOnlinePublished = true;
}
