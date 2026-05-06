// web_ui.cpp
#include "web_ui.h"

#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>

#include "app_state.h"
#include "app_config.h"
#include "sensor_bus.h"
#include "sensor_names.h"
#include "water_probe.h"
#include "mqtt_client.h"
#include "metrics_server.h"
#include "util.h"
#include "display_ui.h"

static void sendJsonDoc(JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  webServer.send(200, "application/json", out);
  yield();
}

static void sendOk(const char* msg = "ok") {
  StaticJsonDocument<192> doc;
  doc["ok"] = true;
  doc["message"] = msg;
  sendJsonDoc(doc);
}

static void sendError(const char* msg, int code = 400) {
  StaticJsonDocument<192> doc;
  doc["ok"] = false;
  doc["message"] = msg ? msg : "error";
  String out;
  serializeJson(doc, out);
  webServer.send(code, "application/json", out);
  yield();
}

static bool streamLittleFsFile(const char* path, const char* contentType) {
  if (!LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  webServer.streamFile(f, contentType);
  f.close();
  yield();
  return true;
}

void handleHomePage() {
  if (streamLittleFsFile("/index.html", "text/html; charset=utf-8")) return;
  webServer.send(500, "text/plain", "index.html missing from LittleFS");
}

void handleAppJs() {
  if (streamLittleFsFile("/app.js", "application/javascript; charset=utf-8")) return;
  webServer.send(404, "text/plain", "app.js not found");
}

void handleStyleCss() {
  if (streamLittleFsFile("/style.css", "text/css; charset=utf-8")) return;
  webServer.send(404, "text/plain", "style.css not found");
}

void handleApiStatus() {
  StaticJsonDocument<1024> doc;
  doc["id"] = safeDeviceId();
  doc["ssid"] = WiFi.SSID();
  doc["ip"] = ipToString(WiFi.localIP());
  doc["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
  doc["mqtt_connected"] = mqtt.connected();
  doc["rssidbm"] = WiFi.RSSI();
  doc["freeheap"] = ESP.getFreeHeap();
  doc["sensorcount"] = sensorCount;
  doc["simulated"] = useFakeSensors;
  doc["networkdetected"] = sensorNetworkDetected;
  doc["prometheusport"] = config.prometheusPort;
  doc["uptime_ms"] = millis() - bootMillis;
  doc["last_status"] = lastStatusMsg;
  doc["last_rx_type"] = lastRxType;
  appendWaterToJson(doc);
  sendJsonDoc(doc);
}

void handleApiTemps() {
  StaticJsonDocument<1536> doc;
  doc["sensorcount"] = sensorCount;
  doc["simulated"] = useFakeSensors;
  doc["networkdetected"] = sensorNetworkDetected;
  doc["last_sample_ms_age"] = lastSensorSampleMs > 0 ? (millis() - lastSensorSampleMs) : 0;
  doc["last_rescan_ms_age"] = lastSensorRescanMs > 0 ? (millis() - lastSensorRescanMs) : 0;

  JsonArray sensors = doc.createNestedArray("sensors");
  for (uint8_t i = 0; i < sensorCount; i++) {
    JsonObject s = sensors.createNestedObject();
    s["index"] = i + 1;
    s["name"] = sensorNames[i];
    s["address"] = sensorAddressString(i);
    s["connected"] = sensorPresent[i];
    if (isnan(sensorTempsC[i])) s["tempc"] = nullptr;
    else s["tempc"] = sensorTempsC[i];
  }

  sendJsonDoc(doc);
}

void handleApiWater() {
  StaticJsonDocument<512> doc;
  appendWaterToJson(doc);
  sendJsonDoc(doc);
}

void handleApiConfig() {
  StaticJsonDocument<1024> doc;
  doc["mqtthost"] = config.mqttHost;
  doc["mqttport"] = config.mqttPort;
  doc["basetopic"] = config.baseTopic;
  doc["deviceid"] = safeDeviceId();
  doc["prometheusport"] = config.prometheusPort;

  JsonObject water = doc.createNestedObject("water");
  water["intervalms"] = config.waterHeartbeatIntervalMs;
  JsonArray thresholds = water.createNestedArray("thresholds");
  for (uint8_t i = 0; i < waterthresholdcount; i++) thresholds.add(config.waterThresholds[i]);

  JsonObject topics = doc.createNestedObject("topics");
  topics["command"] = commandTopic;
  topics["status"] = statusTopic;
  topics["results"] = resultsTopic;
  topics["water"] = waterTopic;

  sendJsonDoc(doc);
}

void handleApiScanSensors() {
  webRequestSensorScan = true;
  setStatusMessage("scan queued", 1200);
  sendOk("scan queued");
}

void handleApiSampleWater() {
  webRequestWaterSample = true;
  setStatusMessage("water queued", 1200);
  sendOk("water sample queued");
}

void handlePostServicesConfig() {
  bool changed = false;

  if (webServer.hasArg("mqtthost")) changed |= setMqttHostValue(webServer.arg("mqtthost").c_str());
  if (webServer.hasArg("mqttport")) changed |= setMqttPortValue((uint16_t)webServer.arg("mqttport").toInt());
  if (webServer.hasArg("basetopic")) changed |= setBaseTopicValue(webServer.arg("basetopic").c_str());
  if (webServer.hasArg("deviceid")) changed |= setDeviceIdValue(webServer.arg("deviceid").c_str());
  if (webServer.hasArg("prometheusport")) changed |= setPrometheusPortValue((uint16_t)webServer.arg("prometheusport").toInt());

  saveConfig();
  setStatusMessage(changed ? "services saved" : "services unchanged", 1500);
  sendOk("services saved");
}

void handlePostWaterConfig() {
  bool ok = true;

  if (webServer.hasArg("intervalms")) {
    ok &= setWaterIntervalMs((uint32_t)webServer.arg("intervalms").toInt());
  }

  uint16_t vals[waterthresholdcount];
  const char* keys[waterthresholdcount] = {"t0", "t1", "t2", "t3", "t4"};
  bool haveAll = true;
  for (uint8_t i = 0; i < waterthresholdcount; i++) {
    if (!webServer.hasArg(keys[i])) {
      haveAll = false;
      break;
    }
    vals[i] = (uint16_t)webServer.arg(keys[i]).toInt();
  }

  if (haveAll) ok &= setWaterThresholdsArray(vals, waterthresholdcount);

  if (!ok) {
    sendError("invalid water config");
    return;
  }

  saveConfig();
  setStatusMessage("water cfg saved", 1500);
  sendOk("water config saved");
}

void handlePostSensorRename() {
  if (!webServer.hasArg("index") || !webServer.hasArg("name")) {
    sendError("missing index or name");
    return;
  }

  uint8_t index1 = (uint8_t)webServer.arg("index").toInt();
  String name = webServer.arg("name");
  name.trim();

  if (!setSensorNameByIndex(index1, name.c_str())) {
    sendError("rename failed");
    return;
  }

  setStatusMessage("sensor renamed", 1500);
  sendOk("sensor renamed");
}

void startMainWebUi() {
  webServer.on("/", HTTP_GET, handleHomePage);
  webServer.on("/index.html", HTTP_GET, handleHomePage);
  webServer.on("/app.js", HTTP_GET, handleAppJs);
  webServer.on("/style.css", HTTP_GET, handleStyleCss);

  webServer.on("/api/status", HTTP_GET, handleApiStatus);
  webServer.on("/api/temps", HTTP_GET, handleApiTemps);
  webServer.on("/api/water", HTTP_GET, handleApiWater);
  webServer.on("/api/config", HTTP_GET, handleApiConfig);

  webServer.on("/api/sensors/scan", HTTP_POST, handleApiScanSensors);
  webServer.on("/api/water/sample", HTTP_POST, handleApiSampleWater);

  webServer.on("/api/config/services", HTTP_POST, handlePostServicesConfig);
  webServer.on("/api/config/water", HTTP_POST, handlePostWaterConfig);
  webServer.on("/api/sensors/rename", HTTP_POST, handlePostSensorRename);

  webServer.onNotFound([]() {
    if (webServer.uri().startsWith("/api/")) {
      sendError("not found", 404);
      return;
    }
    if (LittleFS.exists(webServer.uri())) {
      String path = webServer.uri();
      String type = "text/plain";
      if (path.endsWith(".css")) type = "text/css; charset=utf-8";
      else if (path.endsWith(".js")) type = "application/javascript; charset=utf-8";
      else if (path.endsWith(".html")) type = "text/html; charset=utf-8";
      else if (path.endsWith(".svg")) type = "image/svg+xml";
      else if (path.endsWith(".json")) type = "application/json";
      File f = LittleFS.open(path, "r");
      if (f) {
        webServer.streamFile(f, type);
        f.close();
        return;
      }
    }
    handleHomePage();
  });

  webServer.begin();
}

void serviceMainWebUi() {
  webServer.handleClient();
}

void serviceDeferredWebActions() {
  if (webRequestSensorScan) {
    webRequestSensorScan = false;
    setStatusMessage("scan running", 1200);
    scanSensors(true);
    yield();
    readTemperatures();
    lastSensorSampleMs = millis();
    yield();
  }

  if (webRequestWaterSample) {
    webRequestWaterSample = false;
    setStatusMessage("water running", 1200);
    sampleWaterLevel();
    yield();
  }
}
