#pragma once
#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_SSD1306.h>
#include "pins_and_constants.h"

#ifdef SHARED_LIB_USE_ONEWIRE
  #include <OneWire.h>
  #include <DallasTemperature.h>
struct SensorNameRecord {
  char address[17];
  char name[sensornamelen];
};
extern OneWire oneWire;
extern DallasTemperature ds;
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
extern unsigned long lastSensorHeartbeatMs;
extern unsigned long lastSensorSampleMs;
extern unsigned long lastSensorRescanMs;
extern uint8_t displayStartSensor;
#endif

extern Adafruit_SSD1306 display;
extern WiFiClient wifiClient;
extern PubSubClient mqtt;

extern ESP8266WebServer webServer;
extern ESP8266WebServer* metricsServer;

extern bool shouldSaveConfig;
extern bool portalActive;
extern bool mqttOnlinePublished;
extern bool timeConfigured;

extern unsigned long lastAggregateHeartbeatMs;
extern unsigned long lastDisplayMs;
extern unsigned long lastMqttAttemptMs;
extern unsigned long lastTrafficAnimMs;
extern unsigned long statusMsgUntilMs;
extern unsigned long bootMillis;
extern unsigned long mqttPublishCount;
extern unsigned long metricsScrapeCount;

extern int lastRssi;
extern bool mqttTrafficActive;
extern uint8_t spinnerFrame;
extern String lastRxType;
extern String lastStatusMsg;
extern String lastRxRaw;

#ifdef SHARED_LIB_USE_WATER_PROBE
extern unsigned long lastWaterHeartbeatMs;
extern unsigned long lastWaterSampleMs;
extern uint16_t waterAdcRaw;
extern float waterVoltage;
extern uint8_t waterLevelIndex;
extern bool waterValid;
extern bool waterProbePresent;
extern bool waterProbing;
extern volatile bool webRequestWaterSample;
#endif

extern bool startupDisplayActive;
extern unsigned long startupDisplayUntilMs;

#ifdef SHARED_LIB_USE_ONEWIRE
extern volatile bool webRequestSensorScan;
#endif
extern String webConsoleCommandPending;

// Build version stamp: "YYYYMMDD-HHmmss-abcdef" injected at compile time
extern const char* buildVersion;
