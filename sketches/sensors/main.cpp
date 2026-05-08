#include <Arduino.h>
#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <Wire.h>

#include "pins_and_constants.h"
#include "app_config.h"
#include "app_state.h"
#include "util.h"
#include "sensor_names.h"
#include "sensor_bus.h"
#include "water_probe.h"
#include "wifi_portal.h"
#include "web_ui.h"
#include "metrics_server.h"
#include "mqtt_client.h"
#include "mqtt_publish.h"
#include "display_ui.h"
#include "scheduler.h"
#include "mqtt_commands.h"
#include "console_log.h"

void registerSensorsUiHooks();

static void onMqttConnected() {
  scanSensors(true);
  readTemperatures();
  beginWaterSample();
  publishAggregateStatus();
  publishPerSensorStatuses();
  mqttOnlinePublished = true;
}

static void onMqttMessage(const String& topic, const String& payload) {
  lastRxRaw = payload;
  if (topic == commandTopic) handleCommandJson(payload);
  else lastRxType = "other";
}

static void onMqttPublish(const char* topic, const char* payload, size_t len, bool ok) {
  String line;
  line.reserve(20 + (topic ? strlen(topic) : 0) + len);
  line += ok ? "PUB " : "PUB-FAIL ";
  if (topic) line += topic;
  line += ' ';
  if (payload && len) line.concat(payload, len);
  consoleLog(ok ? CLOG_TX : CLOG_WARN, line);
}

void setup() {
  Serial.begin(115200);
  delay(50);
  bootMillis = millis();

  initDisplayUi();
  registerSensorsUiHooks();
  setStatusMessage("booting", 1500);

  {
    WifiPortalDisplay dpCb;
    dpCb.showPortal    = showPortalScreen;
    dpCb.showCountdown = showStartupReconfigCountdown;
    dpCb.setStatus     = setStatusMessage;
    setWifiPortalDisplayCallbacks(dpCb);
  }
  {
    MqttClientDisplay mqCb;
    mqCb.kickSpinner = kickActivitySpinner;
    mqCb.setStatus   = setStatusMessage;
    setMqttClientDisplayCallbacks(mqCb);
  }

  setMqttMessageHandler(onMqttMessage);
  setMqttConnectedHandler(onMqttConnected);
  setMqttPublishLogger(onMqttPublish);

  if (!LittleFS.begin()) setStatusMessage("LittleFS fail", 3000);

  loadConfig();
  setupTimeHelpers();

  initWaterProbePins();
  initSensorBus();
  loadSensorNames();
  initMqttClient();

  runStartupPortalIfNeeded("sens");

  startMainWebUi();
  startMetricsServer();
  startMqttIfWifiReady();
}

void loop() {
  serviceWifiPortal();
  serviceMainWebUi();
  serviceMetricsServer();
  serviceMqttClient();
  serviceDeferredWebActions();
  runScheduledTasks();
  updateDisplayUi();
}
