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

  // Register display callbacks for wifi_portal (shared lib can't include display_ui.h)
  {
    WifiPortalDisplay dpCb;
    dpCb.showPortal    = showPortalScreen;
    dpCb.showCountdown = showStartupReconfigCountdown;
    dpCb.setStatus     = setStatusMessage;
    setWifiPortalDisplayCallbacks(dpCb);
  }

  // Register display callbacks for mqtt_client (shared lib can't include display_ui.h)
  {
    MqttClientDisplay mqCb;
    mqCb.kickSpinner = kickActivitySpinner;
    mqCb.setStatus   = setStatusMessage;
    setMqttClientDisplayCallbacks(mqCb);
  }

  if (!LittleFS.begin()) {
    setStatusMessage("LittleFS fail", 3000);
  }

  loadConfig();
  setupTimeHelpers();

  initWaterProbePins();
  initSensorBus();
  loadSensorNames();
  initMqttClient();

  // Startup reconfig countdown — runs inside wifi_portal with WDT feeds
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
