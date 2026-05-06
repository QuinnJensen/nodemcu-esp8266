// temp_network_v5_web_portal.ino
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
#include "display_ui.h"
#include "scheduler.h"
#include "mqtt_commands.h"

void setup() {
  Serial.begin(115200);
  delay(50);

  bootMillis = millis();

  initDisplayUi();
  setStatusMessage("booting", 1500);

  // show the initial startup / countdown screen
  showStartupReconfigCountdown(10);   // or whatever you use
  startupDisplayActive = true;
  startupDisplayUntilMs = millis() + 10000UL; // keep it for 10s

  if (!LittleFS.begin()) {
    setStatusMessage("LittleFS fail", 3000);
  }

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
  initialSampleAndPublish();
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

// $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$
