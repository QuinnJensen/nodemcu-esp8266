// heater_ui.cpp — water_heater hooks for shared display/metrics/web modules
#include <ESP8266WiFi.h>
#include <math.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "app_state.h"
#include "app_config.h"
#include "console_log.h"
#include "display_ui.h"
#include "metrics_server.h"
#include "web_ui.h"
#include "util.h"
#include "heater_state.h"
#include "heater_mqtt.h"
#ifdef SHARED_LIB_USE_ONEWIRE
#include "sensor_bus.h"
#include "sensor_names.h"
#endif

// ── Display body: power state, bar, temps if any ───────────────────────────
static void heaterBody() {
  display.setCursor(0, 18);
  display.print("P ");
  display.print(requestedPowerPct);
  display.print("% ");
  display.print(displayedPowerWatts);
  display.print("W");

  // Power bar
  int x=0, y=29, w=128, h=10;
  display.drawRect(x, y, w, h, SSD1306_WHITE);
  int innerW = w - 2;
  int fillW = (innerW * requestedPowerPct) / 100;
  if (fillW > 0) display.fillRect(x + 1, y + 1, fillW, h - 2, SSD1306_WHITE);
  for (int i = 20; i < 100; i += 20) {
    int tx = x + 1 + (innerW * i) / 100;
    display.drawFastVLine(tx, y + 1, h - 2, SSD1306_BLACK);
  }

  uint8_t simState; uint32_t ticks; uint32_t onTicks;
  noInterrupts();
  simState = isrOutputState; ticks = simTickCount; onTicks = simOnTickCount;
  interrupts();

  display.setCursor(0, 42);
  display.print("SIM:");
  display.print(simState ? "ON " : "OFF");
#ifdef SHARED_LIB_USE_ONEWIRE
  if (sensorCount > 0) {
    display.setCursor(46, 42);
    uint8_t idx = 0;
    String name = sensorNames[idx][0] ? String(sensorNames[idx]) : String("S1");
    if (name.length() > 5) name = name.substring(0, 5);
    display.print(name);
    display.print(" ");
    if (!isnan(sensorTempsC[idx])) {
      display.print(String(sensorTempsC[idx] * 9.0f / 5.0f + 32.0f, 1));
      display.print("F");
    } else display.print("disc");
  } else {
    display.setCursor(70, 42);
    display.print("On:");
    display.print(onTicks % 1000);
  }
#else
  display.setCursor(70, 42);
  display.print("On:");
  display.print(onTicks % 1000);
#endif
}

// ── Metrics extras ─────────────────────────────────────────────────────────
static void heaterMetricsExtra(String& m) {
  String idLabel = prometheusEscaped(safeDeviceId());
  float estWatts = estimateCorrectedWatts(requestedPowerPct);
  float estAmps  = estimateCurrentAmps(estWatts);
  m += "# HELP wh_power_percent Requested SSR power percent (0-100).\n";
  m += "# TYPE wh_power_percent gauge\nwh_power_percent{id=\"" + idLabel + "\"} " + String(requestedPowerPct) + "\n";
  m += "# HELP wh_est_power_watts Estimated heater power in watts.\n";
  m += "# TYPE wh_est_power_watts gauge\nwh_est_power_watts{id=\"" + idLabel + "\"} " + String(estWatts, 2) + "\n";
  m += "# HELP wh_est_current_amps Estimated heater current in amps.\n";
  m += "# TYPE wh_est_current_amps gauge\nwh_est_current_amps{id=\"" + idLabel + "\"} " + String(estAmps, 3) + "\n";
  m += "# HELP wh_calibration_enabled Whether calibration table is in use.\n";
  m += "# TYPE wh_calibration_enabled gauge\nwh_calibration_enabled{id=\"" + idLabel + "\"} " + String(hasAnyCalibration() ? 1 : 0) + "\n";
  uint32_t ticks, onTicks;
  noInterrupts(); ticks = simTickCount; onTicks = simOnTickCount; interrupts();
  m += "# HELP wh_sim_ticks_total Modulator total ticks.\n";
  m += "# TYPE wh_sim_ticks_total counter\nwh_sim_ticks_total{id=\"" + idLabel + "\"} " + String(ticks) + "\n";
  m += "# HELP wh_sim_on_ticks_total Modulator on ticks.\n";
  m += "# TYPE wh_sim_on_ticks_total counter\nwh_sim_on_ticks_total{id=\"" + idLabel + "\"} " + String(onTicks) + "\n";
#ifdef SHARED_LIB_USE_ONEWIRE
  m += "# HELP wh_sensor_count Number of active 1-Wire sensors.\n";
  m += "# TYPE wh_sensor_count gauge\nwh_sensor_count{id=\"" + idLabel + "\"} " + String(sensorCount) + "\n";
  for (uint8_t i = 0; i < sensorCount; i++) {
    String labels = "id=\"" + idLabel + "\",index=\"" + String(i + 1) + "\",name=\"" + prometheusEscaped(String(sensorNames[i])) + "\"";
    if (!isnan(sensorTempsC[i])) {
      m += "wh_sensor_temp_c{" + labels + "} " + String(sensorTempsC[i], 4) + "\n";
      m += "wh_sensor_temp_f{" + labels + "} " + String(sensorTempsC[i] * 9.0f / 5.0f + 32.0f, 4) + "\n";
    }
  }
#endif
}

// ── Status / config JSON extras ─────────────────────────────────────────────
static void heaterStatusJson(JsonDocument& doc) {
  appendHeaterStateToJson(doc);
#ifdef SHARED_LIB_USE_ONEWIRE
  doc["sensorcount"] = sensorCount;
  JsonArray sensors = doc.createNestedArray("sensors");
  for (uint8_t i = 0; i < sensorCount; i++) {
    JsonObject s = sensors.createNestedObject();
    s["index"] = i + 1;
    s["name"]  = sensorNames[i];
    s["address"] = sensorAddressString(i);
    s["connected"] = sensorPresent[i];
    if (!isnan(sensorTempsC[i])) {
      s["tempc"] = sensorTempsC[i];
      s["tempf"] = sensorTempsC[i] * 9.0f / 5.0f + 32.0f;
    }
  }
#endif
}

static void heaterConfigJson(JsonDocument& doc) {
  doc["full_scale_watts"] = WH_FULL_SCALE_WATTS;
  doc["nominal_vrms"]     = WH_NOMINAL_VRMS;
  doc["calibration_enabled"] = hasAnyCalibration();
  JsonArray cal = doc.createNestedArray("calibration_points");
  for (int i = 0; i < WH_CAL_POINTS; i++) cal.add(calTable[i]);
}

// ── /help additions ────────────────────────────────────────────────────────
static bool heaterHelp() {
  consoleLog(CLOG_INFO, "Water heater commands:");
  consoleLog(CLOG_INFO, "  status       \xe2\x80\x94 publish a status snapshot");
  consoleLog(CLOG_INFO, "  ls           \xe2\x80\x94 list LittleFS contents");
  consoleLog(CLOG_INFO, "  power        \xe2\x80\x94 alias for status");
  consoleLog(CLOG_INFO, "Or paste raw JSON, e.g. {\"power_percent\":37}, {\"command\":\"calibrate\",\"actual_power_watts\":612}");
#ifdef SHARED_LIB_USE_ONEWIRE
  consoleLog(CLOG_INFO, "  scan         \xe2\x80\x94 rescan 1-Wire bus + read temps");
#endif
  return true;
}

// ── Custom routes ──────────────────────────────────────────────────────────
static void handleApiPowerPost() {
  if (!webServer.hasArg("percent") && !webServer.hasArg("power_percent")) {
    webSendError("missing percent", 400); return;
  }
  int pct = webServer.hasArg("percent")
            ? webServer.arg("percent").toInt()
            : webServer.arg("power_percent").toInt();
  pct = constrain(pct, 0, 100);
  updatePowerValues(pct);
  lastCommandMs = millis();
  setStatusMessage(("power " + String(pct) + "%").c_str(), 1500);
  webSendOk("power set");
}

static void handleApiHeaterStatus() {
  StaticJsonDocument<1024> doc;
  appendHeaterStateToJson(doc);
  webSendJsonDoc(doc);
}

static void handleApiCalPost() {
  if (!webServer.hasArg("actual_power_watts")) { webSendError("missing actual_power_watts", 400); return; }
  float w = webServer.arg("actual_power_watts").toFloat();
  if (handleCalibrationRequest(w)) { webSendOk("cal stored"); } else { webSendError("cal failed", 400); }
}

static void handleApiCalPurge() {
  purgeCalibrationFile();
  webSendOk("cal purged");
}

#ifdef SHARED_LIB_USE_ONEWIRE
static void handleApiScanPost() {
  webRequestSensorScan = true;
  setStatusMessage("scan queued", 1200);
  webSendOk("scan queued");
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
#endif

static void heaterRoutes() {
  webServer.on("/api/heater/status", HTTP_GET,  handleApiHeaterStatus);
  webServer.on("/api/heater/power",  HTTP_POST, handleApiPowerPost);
  webServer.on("/api/heater/calibrate", HTTP_POST, handleApiCalPost);
  webServer.on("/api/heater/calibrate/purge", HTTP_POST, handleApiCalPurge);
#ifdef SHARED_LIB_USE_ONEWIRE
  webServer.on("/api/sensors/scan",   HTTP_POST, handleApiScanPost);
  webServer.on("/api/sensors/rename", HTTP_POST, handlePostSensorRename);
#endif
}

// ── Deferred actions ───────────────────────────────────────────────────────
extern void serviceHeaterDeferred();
static void heaterDeferred() {
  serviceHeaterDeferred();
}

void registerHeaterUiHooks() {
  setMetricsNamePrefix("wh");
  setMetricsExtra(heaterMetricsExtra);
  setDisplayBodyRenderer(heaterBody);
  setWebStatusJsonFn(heaterStatusJson);
  setWebConfigJsonFn(heaterConfigJson);
  setWebConsoleHelpFn(heaterHelp);
  setWebExtraRoutesFn(heaterRoutes);
  setWebDeferredFn(heaterDeferred);
}
