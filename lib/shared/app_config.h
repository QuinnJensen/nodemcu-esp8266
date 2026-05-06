#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "pins_and_constants.h"

struct AppConfig {
  char mqttHost[64] = "192.168.1.50";
  char baseTopic[64] = "mountain/mcu";
  char deviceId[32] = "newKid";
  uint16_t mqttPort = 1883;
  uint16_t prometheusPort = 9111;
  uint32_t waterHeartbeatIntervalMs = defaultwaterheartbeatintervalms;
  uint16_t waterThresholds[waterthresholdcount] = {20, 44, 268, 485, 1023};
};

extern AppConfig config;
extern char commandTopic[128];
extern char statusTopic[128];
extern char resultsTopic[128];
extern char waterTopic[128];

void buildTopics();
bool loadConfig();
bool saveConfig();
void loadDefaultWaterThresholds();
bool setWaterIntervalMs(uint32_t intervalMs);
bool setWaterThresholdsArray(const uint16_t* vals, uint8_t count);
bool setMqttHostValue(const char* host);
bool setBaseTopicValue(const char* topic);
bool setDeviceIdValue(const char* id);
bool setMqttPortValue(uint16_t port);
bool setPrometheusPortValue(uint16_t port);
