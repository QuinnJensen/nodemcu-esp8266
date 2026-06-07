#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "pins_and_constants.h"

#ifndef SHARED_LIB_DEFAULT_DEVICE_ID
#define SHARED_LIB_DEFAULT_DEVICE_ID "newKid"
#endif
#ifndef SHARED_LIB_DEFAULT_MCU_TOPIC
#define SHARED_LIB_DEFAULT_MCU_TOPIC "stat/mcu"
#endif
#ifndef SHARED_LIB_DEFAULT_SENSOR_TOPIC
#define SHARED_LIB_DEFAULT_SENSOR_TOPIC "stat/w1"
#endif

struct AppConfig {
  char mqttHost[64]  = "192.168.1.50";
  char mcuBaseTopic[64]    = SHARED_LIB_DEFAULT_MCU_TOPIC;
  char sensorBaseTopic[64] = SHARED_LIB_DEFAULT_SENSOR_TOPIC;
  char deviceId[32]  = SHARED_LIB_DEFAULT_DEVICE_ID;
  char timezone[40]  = devicetz;
  uint16_t mqttPort          = 1883;
  uint16_t prometheusPort    = 9111;
  bool ledEnabled = true;
#ifdef SHARED_LIB_USE_WATER_PROBE
  uint32_t waterHeartbeatIntervalMs = defaultwaterheartbeatintervalms;
  uint16_t waterThresholds[waterthresholdcount] = {44, 268, 485};
#endif
};

extern AppConfig config;
extern char commandTopic[128];
extern char statusTopic[128];
extern char resultsTopic[128];
#ifdef SHARED_LIB_USE_WATER_PROBE
extern char waterTopic[128];
#endif

void buildTopics();
bool loadConfig();
bool saveConfig();
bool setMqttHostValue(const char* host);
bool setMcuBaseTopicValue(const char* topic);
bool setSensorBaseTopicValue(const char* topic);
bool setDeviceIdValue(const char* id);
bool setMqttPortValue(uint16_t port);
bool setPrometheusPortValue(uint16_t port);
bool setLedEnabled(bool enabled);
bool setTimezoneValue(const char* tz);

#ifdef SHARED_LIB_USE_WATER_PROBE
void loadDefaultWaterThresholds();
bool setWaterIntervalMs(uint32_t intervalMs);
bool setWaterThresholdsArray(const uint16_t* vals, uint8_t count);
#endif

// Sketch hook: if defined, the config layer will call these to load/save
// sketch-specific JSON fields alongside the shared ones. Sketches register
// these via setAppConfigExtraHooks(). Both may be null.
typedef void (*AppConfigExtraLoadFn)(JsonObjectConst doc);
typedef void (*AppConfigExtraSaveFn)(JsonObject doc);
void setAppConfigExtraHooks(AppConfigExtraLoadFn loadFn, AppConfigExtraSaveFn saveFn);
