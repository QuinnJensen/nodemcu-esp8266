// app_config.cpp
#include "app_config.h"
#include <LittleFS.h>
#include "util.h"

AppConfig config;

char commandTopic[128];
char statusTopic[128];
char resultsTopic[128];
char waterTopic[128];

static const uint16_t waterThresholdDefaultsLocal[waterthresholdcount] = {20, 44, 268, 485, 1023};

void loadDefaultWaterThresholds() {
  for (uint8_t i = 0; i < waterthresholdcount; i++) config.waterThresholds[i] = waterThresholdDefaultsLocal[i];
}

void buildTopics() {
  String idPart = sanitizeTopicPart(safeDeviceId());
  snprintf(commandTopic, sizeof(commandTopic), "%s/%s/command", config.baseTopic, idPart.c_str());
  snprintf(statusTopic, sizeof(statusTopic), "%s/%s/status", config.baseTopic, idPart.c_str());
  snprintf(resultsTopic, sizeof(resultsTopic), "%s/%s/results", config.baseTopic, idPart.c_str());
  snprintf(waterTopic, sizeof(waterTopic), "%s/%s/water", config.baseTopic, idPart.c_str());
}

bool setWaterIntervalMs(uint32_t intervalMs) {
  if (intervalMs < 1000UL || intervalMs > 86400000UL) return false;
  config.waterHeartbeatIntervalMs = intervalMs;
  return true;
}

bool setWaterThresholdsArray(const uint16_t* vals, uint8_t count) {
  if (!vals || count != waterthresholdcount) return false;
  uint16_t prev = 0;
  for (uint8_t i = 0; i < count; i++) {
    if (vals[i] > 1023) return false;
    if (i > 0 && vals[i] < prev) return false;
    prev = vals[i];
  }
  for (uint8_t i = 0; i < count; i++) config.waterThresholds[i] = vals[i];
  return true;
}

bool setMqttHostValue(const char* host) {
  if (!host || !host[0]) return false;
  strlcpy(config.mqttHost, host, sizeof(config.mqttHost));
  return true;
}

bool setBaseTopicValue(const char* topic) {
  if (!topic || !topic[0]) return false;
  strlcpy(config.baseTopic, topic, sizeof(config.baseTopic));
  buildTopics();
  return true;
}

bool setDeviceIdValue(const char* id) {
  if (!id || !id[0]) return false;
  strlcpy(config.deviceId, id, sizeof(config.deviceId));
  buildTopics();
  return true;
}

bool setMqttPortValue(uint16_t port) {
  if (port == 0) return false;
  config.mqttPort = port;
  return true;
}

bool setPrometheusPortValue(uint16_t port) {
  if (port == 0) return false;
  config.prometheusPort = port;
  return true;
}

bool loadConfig() {
  strlcpy(config.mqttHost, "192.168.1.50", sizeof(config.mqttHost));
  strlcpy(config.baseTopic, "mountain/mcu", sizeof(config.baseTopic));
  strlcpy(config.deviceId, "newKid", sizeof(config.deviceId));
  config.mqttPort = 1883;
  config.prometheusPort = 9111;
  config.waterHeartbeatIntervalMs = defaultwaterheartbeatintervalms;
  loadDefaultWaterThresholds();

  if (!LittleFS.exists(configfile)) {
    buildTopics();
    return false;
  }

  File f = LittleFS.open(configfile, "r");
  if (!f) {
    buildTopics();
    return false;
  }

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    buildTopics();
    return false;
  }

  strlcpy(config.mqttHost, doc["mqtthost"] | "192.168.1.50", sizeof(config.mqttHost));
  strlcpy(config.baseTopic, doc["basetopic"] | "mountain/mcu", sizeof(config.baseTopic));
  strlcpy(config.deviceId, doc["deviceid"] | "newKid", sizeof(config.deviceId));
  config.mqttPort = doc["mqttport"] | 1883;
  config.prometheusPort = doc["prometheusport"] | 9111;
  config.waterHeartbeatIntervalMs = doc["waterheartbeatintervalms"] | defaultwaterheartbeatintervalms;
  if (config.waterHeartbeatIntervalMs < 1000UL) config.waterHeartbeatIntervalMs = defaultwaterheartbeatintervalms;

  JsonArrayConst waterThresholds = doc["waterthresholds"].as<JsonArrayConst>();
  if (!waterThresholds.isNull() && waterThresholds.size() == waterthresholdcount) {
    uint16_t vals[waterthresholdcount];
    for (uint8_t i = 0; i < waterthresholdcount; i++) vals[i] = waterThresholds[i] | waterThresholdDefaultsLocal[i];
    setWaterThresholdsArray(vals, waterthresholdcount);
  }

  buildTopics();
  return true;
}

bool saveConfig() {
  StaticJsonDocument<512> doc;
  doc["mqtthost"] = config.mqttHost;
  doc["mqttport"] = config.mqttPort;
  doc["basetopic"] = config.baseTopic;
  doc["deviceid"] = safeDeviceId();
  doc["prometheusport"] = config.prometheusPort;
  doc["waterheartbeatintervalms"] = config.waterHeartbeatIntervalMs;

  JsonArray thresholds = doc.createNestedArray("waterthresholds");
  for (uint8_t i = 0; i < waterthresholdcount; i++) thresholds.add(config.waterThresholds[i]);

  File f = LittleFS.open(configfile, "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  return true;
}
