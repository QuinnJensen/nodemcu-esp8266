#include "wifi_portal.h"
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include "app_state.h"
#include "app_config.h"
#include "util.h"

static WifiPortalDisplay sDisplay;

void setWifiPortalDisplayCallbacks(const WifiPortalDisplay& cb) {
  sDisplay = cb;
}

static void _showPortal(const char* ssid = nullptr) {
  if (sDisplay.showPortal) sDisplay.showPortal(ssid);
}
static void _showCountdown(uint8_t s) {
  if (sDisplay.showCountdown) sDisplay.showCountdown(s);
}
static void _setStatus(const String& msg, unsigned long holdMs = 3000) {
  if (sDisplay.setStatus) sDisplay.setStatus(msg, holdMs);
}

void saveConfigCallback() {
  shouldSaveConfig = true;
}

static void configModeCallback(WiFiManager *myWiFiManager) {
  _showPortal(myWiFiManager->getConfigPortalSSID().c_str());
}

void startPortalAndConnect(bool forcePortal, const char* ssidSuffix) {
  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setAPCallback(configModeCallback);
  wm.setClass("invert");
  wm.setDarkMode(true);
  wm.setConfigPortalTimeout(portaltimeoutsec);
  if (forcePortal) wm.resetSettings();

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char ssid[32];
  snprintf(ssid, sizeof(ssid), "esp%02X%02X-%s", mac[4], mac[5], ssidSuffix);

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
  
  // Pre-set the AP name in the display UI so it's correct from the very first frame
  extern void setDisplayPortalAp(const char* ap); 
  setDisplayPortalAp(ssid);

  bool ok;
  if (forcePortal) ok = wm.startConfigPortal(ssid);
  else ok = wm.autoConnect(ssid);
  portalActive = false;
  if (!ok) {
    _setStatus("portal timeout", 2000);
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
  _setStatus("ip " + ipToString(WiFi.localIP()), 3000);
}

bool startupReconfigRequested() {
  pinMode(forceportalpin, INPUT_PULLUP);
  unsigned long start = millis();
  uint8_t lastShown = 255;
  while (millis() - start < startupreconfigcountdownms) {
    // Feed both SW and HW watchdogs — I2C OLED writes can take
    // long enough to trip the ESP8266 HW watchdog (~3.2s) otherwise
    yield();
    ESP.wdtFeed();
    unsigned long elapsed = millis() - start;
    uint8_t secondsLeft = (uint8_t)((startupreconfigcountdownms - elapsed + 999UL) / 1000UL);
    if (secondsLeft != lastShown) {
      _showCountdown(secondsLeft);
      yield();       // feed WDT again after the blocking OLED write
      lastShown = secondsLeft;
    }
    if (digitalRead(forceportalpin) == LOW) {
      delay(30);
      if (digitalRead(forceportalpin) == LOW) {
        while (digitalRead(forceportalpin) == LOW) { yield(); }
        return true;
      }
    }
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
  _setStatus("connecting wifi", 2000);
  WiFi.reconnect();
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000UL) {
    yield();
  }
  if (WiFi.status() == WL_CONNECTED) {
    lastRssi = WiFi.RSSI();
    _setStatus("ip " + ipToString(WiFi.localIP()), 3000);
  }
}

void runStartupPortalIfNeeded(const char* ssidSuffix) {
  bool forcePortal = forcePortalRequested() || startupReconfigRequested();
  if (forcePortal) {
    _showPortal(nullptr);
    startupDisplayActive = true;
    startupDisplayUntilMs = millis() + 600000UL;
    startPortalAndConnect(true, ssidSuffix);
  } else {
    startPortalAndConnect(false, ssidSuffix);
  }
}

void serviceWifiPortal() {
}
