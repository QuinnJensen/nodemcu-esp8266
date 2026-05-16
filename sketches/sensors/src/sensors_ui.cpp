// sensors_ui.cpp - sensors sketch hooks for the shared display/metrics/web modules
#include <ESP8266WiFi.h>
#include <math.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "app_state.h"
#include "app_config.h"
#include "display_ui.h"
#include "metrics_server.h"
#include "web_ui.h"
#include "console_log.h"
#include "sensor_bus.h"
#include "sensor_names.h"
#include "water_probe.h"
#include "util.h"
#include "mqtt_publish.h"

// ── Display body: sensor list + water line ─────────────────────────────────
static void sensorsDisplayBody() {
  display.setCursor(0, 16);
  display.print("Sensors(");
  display.print(sensorCount);
  display.print(")");
  if (!useFakeSensors && sensorNetworkDetected && sensorCount == 0) display.print(" offline");
  else if (useFakeSensors) display.print(" sim");

  for (uint8_t row = 0; row < 2; row++) {
    uint8_t y = 28 + (row * 10);
    display.setCursor(0, y);
    if (sensorCount == 0) {
      if (row == 0) display.print("no sensors");
      continue;
    }
    uint8_t idx = (displayStartSensor + row) % sensorCount;
    String label = sensorNames[idx][0] ? String(sensorNames[idx]) : String("S") + String(idx + 1);
    if (label.length() > 14) label = label.substring(0, 14);
    display.print(label); display.print(" ");
    if (isnan(sensorTempsC[idx])) display.print("disc");
    else {
      display.print(String(sensorTempsC[idx] * 9.0f / 5.0f + 32.0f, 1));
      display.print("F");
    }
  }
  display.setCursor(0, 48);
  display.print("Water ");
  if (waterProbing) display.print("[probing]");
  else display.print(waterLevelLabel(waterLevelIndex));
}

// ── Metrics extras ─────────────────────────────────────────────────────────
static void sensorsMetricsExtra(String& m) {
  String idLabel = prometheusEscaped(safeDeviceId());
  m += "# HELP temp_sensor_count Number of active sensors.\n";
  m += "# TYPE temp_sensor_count gauge\n";
  m += "temp_sensor_count{id=\"" + idLabel + "\"} " + String(sensorCount) + "\n";
  m += "# HELP temp_sensor_network_detected Real sensor network detected.\n";
  m += "# TYPE temp_sensor_network_detected gauge\n";
  m += String("temp_sensor_network_detected{id=\"") + idLabel + "\"} " + (sensorNetworkDetected ? "1\n" : "0\n");
  m += "# HELP temp_sensor_simulated Simulated sensors active.\n";
  m += "# TYPE temp_sensor_simulated gauge\n";
  m += String("temp_sensor_simulated{id=\"") + idLabel + "\"} " + (useFakeSensors ? "1\n" : "0\n");
  m += "# HELP temp_last_sensor_sample_seconds Seconds since last sensor sample.\n";
  m += "# TYPE temp_last_sensor_sample_seconds gauge\n";
  m += "temp_last_sensor_sample_seconds{id=\"" + idLabel + "\"} " + String((millis() - lastSensorSampleMs) / 1000UL) + "\n";

  String waterLevelLabelEsc = prometheusEscaped(String(waterLevelLabel(waterLevelIndex)));
  String wb = "id=\"" + idLabel + "\",level=\"" + waterLevelLabelEsc + "\"";
  m += "water_probe_present{" + wb + "} " + String(waterProbePresent ? 1 : 0) + "\n";
  m += "water_valid{" + wb + "} " + String(waterValid ? 1 : 0) + "\n";
  m += "water_adc_raw{" + wb + "} " + String(waterAdcRaw) + "\n";
  m += "water_level_index{" + wb + "} " + String(int(waterLevelIndex)) + "\n";
  m += "water_heartbeat_interval_ms{" + wb + "} " + String(config.waterHeartbeatIntervalMs) + "\n";
  m += "water_last_sample_seconds{" + wb + "} " + String(lastWaterSampleMs > 0 ? ((millis() - lastWaterSampleMs) / 1000UL) : 0) + "\n";
  for (uint8_t i = 0; i < waterthresholdcount; i++) {
    m += "water_threshold_adc{id=\"" + idLabel + "\",level=\"" + prometheusEscaped(String(waterLevelLabel(i))) + "\",level_index=\"" + String(i) + "\"} " + String(config.waterThresholds[i]) + "\n";
  }
  for (uint8_t i = 0; i < sensorCount; i++) {
    String labels = "id=\"" + idLabel + "\"";
    labels += ",index=\"" + String(i + 1) + "\"";
    labels += ",address=\"" + prometheusEscaped(sensorAddressString(i)) + "\"";
    labels += ",name=\"" + prometheusEscaped(String(sensorNames[i])) + "\"";
    m += "temp_sensor_connected{" + labels + "} " + String(sensorPresent[i] ? 1 : 0) + "\n";
    if (!isnan(sensorTempsC[i])) {
      m += "temp_sensor_temp_c{" + labels + "} " + String(sensorTempsC[i], 4) + "\n";
      m += "temp_sensor_temp_f{" + labels + "} " + String(sensorTempsC[i] * 9.0f / 5.0f + 32.0f, 4) + "\n";
    }
  }
}

// ── Web /api/status extras ─────────────────────────────────────────────────
static void sensorsStatusJson(JsonDocument& doc) {
  doc["sensorcount"]     = sensorCount;
  doc["simulated"]       = useFakeSensors;
  doc["networkdetected"] = sensorNetworkDetected;
  appendWaterToJson(doc);
}

// ── Web /api/config extras ─────────────────────────────────────────────────
static void sensorsConfigJson(JsonDocument& doc) {
  JsonObject water = doc.createNestedObject("water");
  water["intervalms"] = config.waterHeartbeatIntervalMs;
  JsonArray thresholds = water.createNestedArray("thresholds");
  for (uint8_t i = 0; i < waterthresholdcount; i++) thresholds.add(config.waterThresholds[i]);
  doc["topics"]["water"] = waterTopic;
}

// ── /help additions ────────────────────────────────────────────────────────
static bool sensorsHelp() {
  consoleLog(CLOG_INFO, "Shorthand commands (run via the MQTT command handler):");
  consoleLog(CLOG_INFO, "  scan        \xe2\x80\x94 scan bus + publish all sensor data");
  consoleLog(CLOG_INFO, "  status      \xe2\x80\x94 alias for scan");
  consoleLog(CLOG_INFO, "  heartbeat   \xe2\x80\x94 alias for scan");
  consoleLog(CLOG_INFO, "  water       \xe2\x80\x94 trigger water probe + publish");
  consoleLog(CLOG_INFO, "  waterstatus \xe2\x80\x94 alias for water");
  return true;
}

// ── Sensor-specific routes ─────────────────────────────────────────────────
static void handleApiTemps() {
  DynamicJsonDocument doc(2048);
  doc["sensorcount"]        = sensorCount;
  doc["simulated"]          = useFakeSensors;
  doc["networkdetected"]    = sensorNetworkDetected;
  doc["last_sample_ms_age"] = lastSensorSampleMs > 0 ? (millis() - lastSensorSampleMs) : 0;
  doc["last_rescan_ms_age"] = lastSensorRescanMs > 0 ? (millis() - lastSensorRescanMs) : 0;
  JsonArray sensors = doc.createNestedArray("sensors");
  for (uint8_t i = 0; i < sensorCount; i++) {
    JsonObject s = sensors.createNestedObject();
    s["index"]     = i + 1;
    s["name"]      = sensorNames[i];
    s["address"]   = sensorAddressString(i);
    s["connected"] = sensorPresent[i];
    if (isnan(sensorTempsC[i])) s["tempc"] = nullptr;
    else s["tempc"] = sensorTempsC[i];
  }
  webSendJsonDoc(doc);
}

static void handleApiWater() {
  StaticJsonDocument<512> doc;
  appendWaterToJson(doc);
  webSendJsonDoc(doc);
}

static void handleApiScanSensors() {
  webRequestSensorScan = true;
  setStatusMessage("scan queued", 1200);
  webSendOk("scan queued");
}

static void handleApiSampleWater() {
  webRequestWaterSample = true;
  setStatusMessage("water queued", 1200);
  webSendOk("water sample queued");
}

static void handlePostWaterConfig() {
  bool ok = true;
  if (webServer.hasArg("intervalms"))
    ok &= setWaterIntervalMs((uint32_t)webServer.arg("intervalms").toInt());
  uint16_t vals[waterthresholdcount];
  const char* keys[waterthresholdcount] = {"t0","t1","t2","t3","t4"};
  bool haveAll = true;
  for (uint8_t i = 0; i < waterthresholdcount; i++) {
    if (!webServer.hasArg(keys[i])) { haveAll = false; break; }
    vals[i] = (uint16_t)webServer.arg(keys[i]).toInt();
  }
  if (haveAll) ok &= setWaterThresholdsArray(vals, waterthresholdcount);
  if (!ok) { webSendError("invalid water config", 400); return; }
  saveConfig();
  setStatusMessage("water cfg saved", 1500);
  webSendOk("water config saved");
}

static void handlePostSensorRename() {
  if (!webServer.hasArg("index") || !webServer.hasArg("name")) {
    webSendError("missing index or name", 400); return;
  }
  uint8_t index1 = (uint8_t)webServer.arg("index").toInt();
  String name = webServer.arg("name");
  name.trim();
  if (!setSensorNameByIndex(index1, name.c_str())) { webSendError("rename failed", 400); return; }
  setStatusMessage("sensor renamed", 1500);
  webSendOk("sensor renamed");
}

static void sensorsRoutes() {
  webServer.on("/api/temps",          HTTP_GET,  handleApiTemps);
  webServer.on("/api/water",          HTTP_GET,  handleApiWater);
  webServer.on("/api/sensors/scan",   HTTP_POST, handleApiScanSensors);
  webServer.on("/api/water/sample",   HTTP_POST, handleApiSampleWater);
  webServer.on("/api/config/water",   HTTP_POST, handlePostWaterConfig);
  webServer.on("/api/sensors/rename", HTTP_POST, handlePostSensorRename);
}

// ── Deferred web actions for sensor/water ──────────────────────────────────
static void sensorsDeferred() {
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
    beginWaterSample();
  }
}

void registerSensorsUiHooks() {
  setMetricsNamePrefix("temp");
  setMetricsExtra(sensorsMetricsExtra);
  setDisplayBodyRenderer(sensorsDisplayBody);
  setWebStatusJsonFn(sensorsStatusJson);
  setWebConfigJsonFn(sensorsConfigJson);
  setWebConsoleHelpFn(sensorsHelp);
  setWebExtraRoutesFn(sensorsRoutes);
  setWebDeferredFn(sensorsDeferred);
}
