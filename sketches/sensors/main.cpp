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

// Called by lib/shared/mqtt_client immediately after successful broker connect+subscribe
static void onMqttConnected() {
  // Do a forced scan + blocking read so first retained publish has real data
  scanSensors(true);
  readTemperatures();
  sampleWaterLevel();
  publishAggregateStatus(true);
  publishPerSensorStatuses(true);
  publishWaterStatus(true);
  mqttOnlinePublished = true;
}

// MQTT message router -- called by lib/shared/mqtt_client on every received message
static void onMqttMessage(const String& topic, const String& payload) {
  lastRxRaw = payload;
  if (topic == commandTopic) handleCommandJson(payload);
  else lastRxType = "other";
}

void setup() {
  Serial.begin(115200);
  delay(50);
  bootMillis = millis();

  initDisplayUi();
  setStatusMessage("booting", 1500);

  // Register display callbacks (shared libs can't include sketch-local display_ui.h)
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

  // Route incoming MQTT messages and connect event to sketch handlers
  setMqttMessageHandler(onMqttMessage);
  setMqttConnectedHandler(onMqttConnected);

  if (!LittleFS.begin()) setStatusMessage("LittleFS fail", 3000);

  loadConfig();
  setupTimeHelpers();

  initWaterProbePins();
  initSensorBus();
  loadSensorNames();
  initMqttClient();

  runStartupPortalIfNeeded();

  startMainWebUi();
  startMetricsServer();
  startMqttIfWifiReady();
  // NOTE: no initialSampleAndPublish() here -- MQTT not connected yet.
  // onMqttConnected() fires automatically after first successful broker connect.
}

void loop() {
  serviceWifiPortal();
  serviceMainWebUi();
  serviceMetricsServer();
  serviceMqttClient();   // calls mqttConnect() -> fires onMqttConnected() on success
  serviceDeferredWebActions();
  runScheduledTasks();
  updateDisplayUi();
}
