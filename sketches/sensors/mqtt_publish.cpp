// mqtt_publish.cpp -- sensor/water domain publish functions
#include "mqtt_publish.h"
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include "app_state.h"
#include "app_config.h"
#include "sensor_names.h"
#include "water_probe.h"
#include "sensor_bus.h"
#include "util.h"
#include "mqtt_client.h"
#include "display_ui.h"

void publishAggregateStatus(bool retained) {
  Serial.print("heartbeat: mqtt connected=");
  Serial.println(mqtt.connected());
  if (!mqtt.connected()) return;

  // static: keeps this off the stack (ESP8266 has only ~4KB total stack)
  static StaticJsonDocument<2048> doc;
  doc.clear();

  doc["type"]             = "status";
  doc["id"]               = safeDeviceId();
  doc["chipid"]           = String(ESP.getChipId(), HEX);
  doc["mac"]              = WiFi.macAddress();
  doc["online"]           = true;

  // WiFi
  doc["ssid"]             = WiFi.SSID();
  doc["rssidbm"]          = WiFi.RSSI();
  doc["ip"]               = ipToString(WiFi.localIP());

  // System
  doc["freeheap"]         = ESP.getFreeHeap();
  doc["uptime_s"]         = millis() / 1000UL;
  doc["mqttpublishcount"] = mqttPublishCount;
  doc["prometheusport"]   = config.prometheusPort;

  // Sensor bus
  doc["sensorcount"]      = sensorCount;
  doc["simulated"]        = useFakeSensors;
  doc["networkdetected"]  = sensorNetworkDetected;

  String ts = currentTimestampString();
  if (ts.length()) doc["timestamp"] = ts;

  appendWaterToJson(doc);

  JsonArray sensors = doc.createNestedArray("sensors");
  for (uint8_t i = 0; i < sensorCount; i++) {
    JsonObject s = sensors.createNestedObject();
    s["index"]     = i + 1;
    s["name"]      = sensorNames[i];
    s["address"]   = sensorAddressString(i);
    s["connected"] = sensorPresent[i];
    if (!isnan(sensorTempsC[i])) {
      s["tempc"] = sensorTempsC[i];
      s["tempf"] = sensorTempsC[i] * 9.0f / 5.0f + 32.0f;
    }
  }

  Serial.print("[MQTT] aggregate doc size=");
  Serial.println(measureJson(doc));

  if (publishJsonDocToTopic(statusTopic, doc, retained))
    setStatusMessage("publishing agg", 1500);
}

void publishPerSensorStatus(uint8_t i, bool retained) {
  if (!mqtt.connected() || i >= sensorCount) return;

  static StaticJsonDocument<512> doc;
  doc.clear();

  doc["type"]      = "sensor";
  doc["id"]        = safeDeviceId();
  doc["online"]    = true;
  doc["index"]     = i + 1;
  doc["name"]      = sensorNames[i];
  doc["address"]   = sensorAddressString(i);
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
  for (uint8_t i = 0; i < sensorCount; i++)
    publishPerSensorStatus(i, retained);
}

void publishWaterStatus(bool retained) {
  if (!mqtt.connected()) return;

  static StaticJsonDocument<512> doc;
  doc.clear();

  doc["type"]   = "water";
  doc["id"]     = safeDeviceId();
  doc["online"] = true;

  String ts = currentTimestampString();
  if (ts.length()) doc["timestamp"] = ts;

  appendWaterToJson(doc);

  if (!publishJsonDocToTopic(waterTopic, doc, retained))
    Serial.println("[MQTT] water publish failed");
}

void publishCommandResult(const char* type, bool ok, const char* msg) {
  if (!mqtt.connected()) return;

  static StaticJsonDocument<256> reply;
  reply.clear();

  reply["type"]    = type ? type : "result";
  reply["id"]      = safeDeviceId();
  reply["ok"]      = ok;
  reply["message"] = msg ? msg : "";

  if (!publishJsonDocToTopic(resultsTopic, reply, false))
    Serial.println("[MQTT] command result publish failed");
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
