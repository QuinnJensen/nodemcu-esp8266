#include <Arduino.h>
#include <LittleFS.h>
#include <ESP8266WiFi.h>

#include "pins_and_constants.h"
#include "app_config.h"
#include "app_state.h"
#include "util.h"
#include "wifi_portal.h"
#include "web_ui.h"
#include "metrics_server.h"
#include "mqtt_client.h"
#include "display_ui.h"
#include "console_log.h"

#include "heater_state.h"
#include "heater_mqtt.h"
#include "ota_update.h"
#ifdef SHARED_LIB_USE_ONEWIRE
#include "sensor_bus.h"
#include "sensor_names.h"
#endif

void registerHeaterUiHooks();

#define WH_HEARTBEAT_INTERVAL_MS 30000UL

static unsigned long lastHeartbeatMs = 0;

static void onMqttConnected() {
  publishHeaterStatus(true);
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

  startModulator();              // Timer1 SSR Bresenham starts immediately

  initDisplayUi();
  registerHeaterUiHooks();
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

#ifdef SHARED_LIB_USE_ONEWIRE
  initSensorBus();
  loadSensorNames();
#endif

  loadCalibration();
  initMqttClient();
  initOtaUpdate();

  // Safety: halt SSR during OTA updates
  setOtaStartCallback([]() {
    isrThermalHalt = true;
    consoleLog(CLOG_WARN, "OTA: Halting SSR for safety");
  });

  runStartupPortalIfNeeded("heat");

  startMainWebUi();
  startMetricsServer();
  startMqttIfWifiReady();

  lastCommandMs = millis();
  powerLevelChangedMs = millis();
  updatePowerValues(0);
  setStatusMessage("heater ready", 2000);
}

static unsigned long lastRssiMs = 0;
#ifdef SHARED_LIB_USE_ONEWIRE
static bool waitingTempCollect = false;
extern bool pendingScan;
#endif

void loop() {
  serviceModulatorOneShot();
  serviceOtaUpdate();
  serviceWifiPortal();
  serviceMainWebUi();
  serviceMetricsServer();
  serviceMqttClient();
  serviceDeferredWebActions();
  updateDisplayUi();

  unsigned long now = millis();

  if (now - lastRssiMs >= 1000) {
    lastRssi = WiFi.RSSI();
    lastRssiMs = now;
  }

  if (mqtt.connected() && now - lastHeartbeatMs >= WH_HEARTBEAT_INTERVAL_MS) {
    lastHeartbeatMs = now;
    publishHeaterStatus(false);
  }

#ifdef SHARED_LIB_USE_ONEWIRE
  // Async 1-Wire Task Machine
  // Prevents heartbeat scans from colliding with manual web/MQTT requests
  bool heartbeatDue = (now - lastSensorHeartbeatMs >= sensorheartbeatintervalms);
  bool manualRequest = (webRequestSensorScan || pendingScan);

  if (waitingTempCollect && conversionPending && now - conversionRequestedMs >= 800) {
    collectTemperatureResults();
    waitingTempCollect = false;

    // Thermal watchdog check
    bool overTemp = false;
    bool disconnected = (sensorCount > 0); // Start true if we expect sensors
    for (uint8_t i = 0; i < sensorCount; i++) {
      if (!sensorPresent[i] || isnan(sensorTempsC[i])) { disconnected = true; break; }
      else disconnected = false; // At least one is ok

      if (sensorTempsC[i] > 60.0f) { overTemp = true; break; } // Safety limit
    }
    
    // Default gate to zero if unsafe
    if (disconnected || overTemp) {
      if (!isrThermalHalt) consoleLog(CLOG_WARN, overTemp ? "SAFETY: Over-temperature!" : "SAFETY: Sensors lost!");
      isrThermalHalt = true;
    } else {
      isrThermalHalt = false;
    }

    // If this was a manual request, publish update immediately
    if (manualRequest) publishHeaterStatus(false);
  }

  if (!waitingTempCollect && (heartbeatDue || manualRequest)) {
    bool forceScan = manualRequest;
    webRequestSensorScan = false;
    pendingScan = false;
    
    if (forceScan) setStatusMessage("scan running", 1200);
    
    scanSensors(forceScan);
    requestTemperatureConversion();
    waitingTempCollect = true;
    lastSensorHeartbeatMs = now;
  }
#endif

  // Refresh display power readout occasionally
  static unsigned long lastRefresh = 0;
  if (now - lastRefresh >= 1000) {
    lastRefresh = now;
    refreshDisplayedPower();
  }
}
