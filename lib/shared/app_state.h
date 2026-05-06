#pragma once
#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_SSD1306.h>
#include "pins_and_constants.h"

struct SensorNameRecord {
  char address[17];
  char name[sensornamelen];
};

extern Adafruit_SSD1306 display;
extern WiFiClient wifiClient;
extern PubSubClient mqtt;
extern OneWire oneWire;
extern DallasTemperature ds;

extern ESP8266WebServer webServer;
extern ESP8266WebServer* metricsServer;

extern DeviceAddress sensorAddresses[maxsensors];
extern float sensorTempsC[maxsensors];
extern bool sensorPresent[maxsensors];
extern char sensorNames[maxsensors][sensornamelen];
extern uint8_t sensorCount;
extern bool sensorNetworkDetected;
extern bool useFakeSensors;
extern bool everHadPhysicalSensors;

extern SensorNameRecord sensorNameRecords[maxsensors];
extern uint8_t sensorNameRecordCount;

extern bool shouldSaveConfig;
extern bool portalActive;
extern bool mqttOnlinePublished;
extern bool timeConfigured;

extern unsigned long lastAggregateHeartbeatMs;
extern unsigned long lastSensorHeartbeatMs;
extern unsigned long lastWaterHeartbeatMs;
extern unsigned long lastDisplayMs;
extern unsigned long lastMqttAttemptMs;
extern unsigned long lastTrafficAnimMs;
extern unsigned long statusMsgUntilMs;
extern unsigned long lastSensorSampleMs;
extern unsigned long lastSensorRescanMs;
extern unsigned long lastWaterSampleMs;
extern unsigned long bootMillis;
extern unsigned long mqttPublishCount;
extern unsigned long metricsScrapeCount;

extern int lastRssi;
extern bool mqttTrafficActive;
extern uint8_t spinnerFrame;
extern uint8_t displayStartSensor;
extern String lastRxType;
extern String lastStatusMsg;
extern String lastRxRaw;

extern uint16_t waterAdcRaw;
extern uint8_t waterLevelIndex;
extern bool waterValid;
extern bool waterProbePresent;

extern bool startupDisplayActive;
extern unsigned long startupDisplayUntilMs;

extern volatile bool webRequestSensorScan;
extern volatile bool webRequestWaterSample;
