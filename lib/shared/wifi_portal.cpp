#include "wifi_portal.h"
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include "app_state.h"
#include "app_config.h"
#include "display_ui.h"
#include "util.h"

void saveConfigCallback() {
  shouldSaveConfig = true;
}

void startPortalAndConnect(bool forcePortal) {
  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setClass("invert");
  wm.setDarkMode(true);
  wm.setConfigPortalTimeout(portaltimeoutsec);
  if (forcePortal) wm.resetSettings();
  char mqttPortBuf[8];
  char promPortBuf[8];
  snprintf(mqttPortBuf, sizeof(mqttPortBuf), "%u", config.mqttPort);
  snprintf(promPortBuf, sizeof(promPortBuf), "%u", config.prometheusPort);
  WiFiManagerParameter pMqttHost("mqtthost", "MQTT broker", config.mqttHost, sizeof(config.mqttHost));
  WiFiManagerParameter pMqttPort("mqttport", "MQTT port", mqttPortBuf, sizeof(mqttPortBuf));
  WiFiManagerParameter pBaseTopic("basetopic", "Base topic", config.baseTopic, sizeof(config.baseTopic));
  WiFiManagerParameter pDeviceId("deviceid", "Device ID", config.deviceId, sizeof(config.deviceId));
  WiFiManagerParameter pPromPort("prometheusport", "Prometheus port", promPortBuf, sizeof(promPortBuf));
  wm.addParameter(&pMqttHost);
  wm.addParameter(&pMqttPort);
  wm.addParameter(&pBaseTopic);
  wm.addParameter(&pDeviceId);
  wm.addParameter(&pPromPort);
  portalActive = true;
  showPortalScreen();
  bool ok;
  if (forcePortal) ok = wm.startConfigPortal("TempSensorSetup");
  else ok = wm.autoConnect("TempSensorSetup");
  portalActive = false;
  if (!ok) {
    setStatusMessage("portal timeout", 2000);
    delay(1000);
    ESP.restart();
    delay(1000);
  }
  strlcpy(config.mqttHost, pMqttHost.getValue(), sizeof(config.mqttHost));
  strlcpy(config.baseTopic, pBaseTopic.getValue(), sizeof(config.baseTopic));
  strlcpy(config.deviceId, pDeviceId.getValue(), sizeof(config.deviceId));
  if (!config.deviceId[0]) strlcpy(config.deviceId, "newKid", sizeof(config.deviceId));
  config.mqttPort = uint16_t(atoi(pMqttPort.getValue()));
  if (config.mqttPort == 0) config.mqttPort = 1883;
  config.prometheusPort = uint16_t(atoi(pPromPort.getValue()));
  if (config.prometheusPort == 0) config.prometheusPort = 9111;
  buildTopics();
  if (shouldSaveConfig) {
    saveConfig();
    shouldSaveConfig = false;
  }
  setStatusMessage("ip " + ipToString(WiFi.localIP()), 3000);
}


bool startupReconfigRequested() {
  pinMode(forceportalpin, INPUT_PULLUP);
  unsigned long start = millis();
  uint8_t lastShown = 255;
  while (millis() - start < startupreconfigcountdownms) {
    unsigned long elapsed = millis() - start;
    uint8_t secondsLeft = (uint8_t)((startupreconfigcountdownms - elapsed + 999UL) / 1000UL);
    if (secondsLeft != lastShown) {
      showStartupReconfigCountdown(secondsLeft);
      lastShown = secondsLeft;
    }
    if (digitalRead(forceportalpin) == LOW) {
      delay(30);
      if (digitalRead(forceportalpin) == LOW) {
        while (digitalRead(forceportalpin) == LOW) delay(10);
        return true;
      }
    }
    delay(10);
  }
  return false;
}

bool forcePortalRequested() {
  pinMode(forceportalpin, INPUT_PULLUP);
  delay(10);
  return digitalRead(forceportalpin) == LOW;
}

void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    lastRssi = WiFi.RSSI();
    return;
  }
  setStatusMessage("connecting wifi", 2000);
  WiFi.reconnect();
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000UL) delay(100);
  if (WiFi.status() == WL_CONNECTED) {
    lastRssi = WiFi.RSSI();
    setStatusMessage("ip " + ipToString(WiFi.localIP()), 3000);
  }
}

void runStartupPortalIfNeeded() {
  bool forcePortal = forcePortalRequested() || startupReconfigRequested();
  if (forcePortal) {
    showPortalScreen();
    startupDisplayActive = true;
    startupDisplayUntilMs = millis() + 600000UL; // keep portal screen up
    startPortalAndConnect(true);
  } else {
    startPortalAndConnect(false);
  }
}

void serviceWifiPortal() {
}
