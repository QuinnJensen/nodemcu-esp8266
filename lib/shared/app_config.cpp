// app_config.cpp
#include "app_config.h"
#include <LittleFS.h>
#include <time.h>
#include "util.h"

AppConfig config;

char commandTopic[128];
char statusTopic[128];
char resultsTopic[128];
#ifdef SHARED_LIB_USE_WATER_PROBE
char waterTopic[128];
static const uint16_t waterThresholdDefaultsLocal[waterthresholdcount] = {44, 268, 485};

void loadDefaultWaterThresholds() {
  for (uint8_t i = 0; i < waterthresholdcount; i++) config.waterThresholds[i] = waterThresholdDefaultsLocal[i];
}
#endif

static AppConfigExtraLoadFn sExtraLoad = nullptr;
static AppConfigExtraSaveFn sExtraSave = nullptr;
void setAppConfigExtraHooks(AppConfigExtraLoadFn loadFn, AppConfigExtraSaveFn saveFn) {
  sExtraLoad = loadFn;
  sExtraSave = saveFn;
}

void buildTopics() {
  String idPart = sanitizeTopicPart(safeDeviceId());
  snprintf(commandTopic, sizeof(commandTopic), "%s/%s/command", config.mcuBaseTopic, idPart.c_str());
  snprintf(statusTopic,  sizeof(statusTopic),  "%s/%s/status",  config.mcuBaseTopic, idPart.c_str());
  snprintf(resultsTopic, sizeof(resultsTopic), "%s/%s/results", config.mcuBaseTopic, idPart.c_str());
#ifdef SHARED_LIB_USE_WATER_PROBE
  snprintf(waterTopic,   sizeof(waterTopic),   "%s/%s/water",   config.mcuBaseTopic, idPart.c_str());
#endif
}

bool setLedEnabled(bool enabled) {
  config.ledEnabled = enabled;
  return true;
}

#ifdef SHARED_LIB_USE_WATER_PROBE
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
#endif

bool setMqttHostValue(const char* host) {
  if (!host || !host[0]) return false;
  strlcpy(config.mqttHost, host, sizeof(config.mqttHost));
  return true;
}

bool setMcuBaseTopicValue(const char* topic) {
  if (!topic || !topic[0]) return false;
  strlcpy(config.mcuBaseTopic, topic, sizeof(config.mcuBaseTopic));
  buildTopics();
  return true;
}

bool setSensorBaseTopicValue(const char* topic) {
  if (!topic || !topic[0]) return false;
  strlcpy(config.sensorBaseTopic, topic, sizeof(config.sensorBaseTopic));
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

bool setTimezoneValue(const char* tz) {
  if (!tz || !tz[0]) return false;
  strlcpy(config.timezone, tz, sizeof(config.timezone));
  setenv("TZ", config.timezone, 1);
  tzset();
  return true;
}

bool loadConfig() {
  // Defaults
  strlcpy(config.mqttHost,  "192.168.1.50", sizeof(config.mqttHost));
  strlcpy(config.mcuBaseTopic,    SHARED_LIB_DEFAULT_MCU_TOPIC,    sizeof(config.mcuBaseTopic));
  strlcpy(config.sensorBaseTopic, SHARED_LIB_DEFAULT_SENSOR_TOPIC, sizeof(config.sensorBaseTopic));
  strlcpy(config.deviceId,  SHARED_LIB_DEFAULT_DEVICE_ID,  sizeof(config.deviceId));
  strlcpy(config.timezone,  devicetz,       sizeof(config.timezone));
  config.mqttPort = 1883;
  config.prometheusPort = 9111;
  config.ledEnabled = true;
  config.waterProbeEnabled = true;
  config.sensorNetworkEnabled = true;
#ifdef SHARED_LIB_USE_WATER_PROBE
  config.waterHeartbeatIntervalMs = defaultwaterheartbeatintervalms;
  loadDefaultWaterThresholds();
#endif

  if (!LittleFS.exists(configfile)) { buildTopics(); return false; }

  File f = LittleFS.open(configfile, "r");
  if (!f) { buildTopics(); return false; }

  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) { buildTopics(); return false; }

  strlcpy(config.mqttHost,  doc["mqtthost"]  | "192.168.1.50", sizeof(config.mqttHost));
  strlcpy(config.mcuBaseTopic,    doc["mcubasetopic"]    | doc["controlbasetopic"] | doc["basetopic"] | SHARED_LIB_DEFAULT_MCU_TOPIC, sizeof(config.mcuBaseTopic));
  strlcpy(config.sensorBaseTopic, doc["sensorbasetopic"] | SHARED_LIB_DEFAULT_SENSOR_TOPIC,  sizeof(config.sensorBaseTopic));
  strlcpy(config.deviceId,  doc["deviceid"]  | SHARED_LIB_DEFAULT_DEVICE_ID, sizeof(config.deviceId));
  strlcpy(config.timezone,  doc["timezone"]  | devicetz,       sizeof(config.timezone));
  config.mqttPort           = doc["mqttport"]           | 1883;
  config.prometheusPort     = doc["prometheusport"]     | 9111;
  config.ledEnabled = doc["ledenabled"] | true;
  config.waterProbeEnabled = doc["water_probe_enabled"] | true;
  config.sensorNetworkEnabled = doc["sensor_network_enabled"] | true;

#ifdef SHARED_LIB_USE_WATER_PROBE
  config.waterHeartbeatIntervalMs = doc["waterheartbeatintervalms"] | defaultwaterheartbeatintervalms;
  if (config.waterHeartbeatIntervalMs < 1000UL) config.waterHeartbeatIntervalMs = defaultwaterheartbeatintervalms;
  JsonArrayConst wt = doc["waterthresholds"].as<JsonArrayConst>();
  if (!wt.isNull() && wt.size() == waterthresholdcount) {
    uint16_t vals[waterthresholdcount];
    for (uint8_t i = 0; i < waterthresholdcount; i++) vals[i] = wt[i] | waterThresholdDefaultsLocal[i];
    setWaterThresholdsArray(vals, waterthresholdcount);
  }
#endif

  if (sExtraLoad) sExtraLoad(doc.as<JsonObjectConst>());

  buildTopics();
  return true;
}

bool saveConfig() {
  StaticJsonDocument<1024> doc;
  doc["mqtthost"]               = config.mqttHost;
  doc["mqttport"]               = config.mqttPort;
  doc["mcubasetopic"]           = config.mcuBaseTopic;
  doc["sensorbasetopic"]        = config.sensorBaseTopic;
  doc["deviceid"]               = safeDeviceId();
  doc["timezone"]               = config.timezone;
  doc["prometheusport"]         = config.prometheusPort;
  doc["ledenabled"]             = config.ledEnabled;
  doc["water_probe_enabled"]     = config.waterProbeEnabled;
  doc["sensor_network_enabled"]   = config.sensorNetworkEnabled;

#ifdef SHARED_LIB_USE_WATER_PROBE
  doc["waterheartbeatintervalms"] = config.waterHeartbeatIntervalMs;
  JsonArray thresholds = doc.createNestedArray("waterthresholds");
  for (uint8_t i = 0; i < waterthresholdcount; i++) thresholds.add(config.waterThresholds[i]);
#endif

  if (sExtraSave) sExtraSave(doc.as<JsonObject>());

  File f = LittleFS.open(configfile, "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  return true;
}
