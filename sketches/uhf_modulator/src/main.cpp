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

#include "uhf_codec.h"
#include "uhf_mqtt.h"
#ifdef SHARED_LIB_USE_ONEWIRE
#include "sensor_bus.h"
#include "sensor_names.h"
#endif

void registerUhfUiHooks();

#define UHF_HEARTBEAT_INTERVAL_MS 60000UL

static unsigned long lastHeartbeatMs = 0;

static void onMqttConnected() {
  publishUhfStatus(true);
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

  initUhfIo();
  initDisplayUi();
  registerUhfUiHooks();
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

  loadProfiles();
  loadCodes();

  initMqttClient();
  runStartupPortalIfNeeded("uhf");

  startMainWebUi();
  startMetricsServer();
  startMqttIfWifiReady();

  setStatusMessage("uhf ready", 2000);
}

static unsigned long lastRssiMs = 0;
#ifdef SHARED_LIB_USE_ONEWIRE
static bool waitingTempCollect = false;
#endif

void loop() {
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

  if (mqtt.connected() && now - lastHeartbeatMs >= UHF_HEARTBEAT_INTERVAL_MS) {
    lastHeartbeatMs = now;
    publishUhfStatus(false);
  }

#ifdef SHARED_LIB_USE_ONEWIRE
  if (waitingTempCollect && conversionPending && now - conversionRequestedMs >= 800) {
    collectTemperatureResults();
    waitingTempCollect = false;
  }
  if (now - lastSensorHeartbeatMs >= sensorheartbeatintervalms) {
    scanSensors();
    requestTemperatureConversion();
    waitingTempCollect = true;
    lastSensorHeartbeatMs = now;
  }
#endif
}
