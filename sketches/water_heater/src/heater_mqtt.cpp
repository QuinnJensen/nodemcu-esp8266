// heater_mqtt.cpp — water_heater MQTT publish + command handler
#include "heater_mqtt.h"
#include "heater_state.h"
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include "app_state.h"
#include "app_config.h"
#include "mqtt_client.h"
#include "util.h"
#include "console_log.h"
#include "display_ui.h"
#ifdef SHARED_LIB_USE_ONEWIRE
#include "sensor_bus.h"
#include "sensor_names.h"
#endif

// Deferred command flags (handled outside callback, after MQTT loop returns)
static bool pendingLs = false;
static bool pendingCalibration = false;
static bool pendingPurge = false;
static float pendingActualWatts = 0.0f;

void publishCommandResult(const char* type, bool ok, const char* message) {
  if (!mqtt.connected()) return;
  StaticJsonDocument<256> reply;
  reply["type"]    = type ? type : "result";
  reply["id"]      = safeDeviceId();
  reply["ok"]      = ok;
  reply["message"] = message ? message : "";
  publishJsonDocToTopic(resultsTopic, reply, false);
}

void publishHeaterStatus(bool retained) {
  if (!mqtt.connected()) {
    consoleLog(CLOG_WARN, "[TX] publishHeaterStatus: MQTT not connected, skipping.");
    return;
  }
  DynamicJsonDocument doc(2048);
  doc["type"]   = "status";
  doc["id"]     = safeDeviceId();
  doc["online"] = true;
  doc["build_version"] = buildVersion;
  doc["ssid"]   = WiFi.SSID();
  doc["rssidbm"] = WiFi.RSSI();
  doc["ip"]      = ipToString(WiFi.localIP());
  doc["freeheap"] = ESP.getFreeHeap();
  doc["uptime_s"] = millis() / 1000UL;
  doc["mqttpublishcount"] = mqttPublishCount;
  doc["prometheusport"] = config.prometheusPort;
  String ts = currentTimestampString();
  if (ts.length()) doc["timestamp"] = ts;
  appendHeaterStateToJson(doc);
#ifdef SHARED_LIB_USE_ONEWIRE
  doc["sensorcount"] = sensorCount;
  JsonArray sensors = doc.createNestedArray("sensors");
  for (uint8_t i = 0; i < sensorCount; i++) {
    JsonObject s = sensors.createNestedObject();
    s["index"]     = i + 1;
    s["name"]      = sensorNames[i];
    s["address"]   = sensorAddressString(i);
    s["connected"] = sensorPresent[i];
    if (!isnan(sensorTempsC[i])) {
      s["tempc"] = sensorTempsC[i];
      s["tempf"] = sensorTempsC[i] * 9.0f / 5.0f + 32.0f;
    }
  }
#endif
  publishJsonDocToTopic(statusTopic, doc, retained);
}

void publishFilesystemListing() {
  FSInfo info;
  DynamicJsonDocument doc(2048);
  doc["type"] = "ls_reply";
  doc["id"]   = safeDeviceId();
  if (!LittleFS.info(info)) {
    doc["ok"] = false;
    doc["error"] = "fs_info_failed";
    publishJsonDocToTopic(resultsTopic, doc, false);
    return;
  }
  doc["ok"] = true;
  String ts = currentTimestampString();
  if (ts.length()) doc["timestamp"] = ts;
  doc["fs_total_bytes"] = info.totalBytes;
  doc["fs_used_bytes"]  = info.usedBytes;
  doc["fs_free_bytes"]  = info.totalBytes - info.usedBytes;
  doc["calibration_enabled"] = hasAnyCalibration();
  JsonArray files = doc.createNestedArray("files");
  uint16_t count = 0;
  Dir dir = LittleFS.openDir("/");
  while (dir.next()) {
    JsonObject e = files.createNestedObject();
    e["name"] = dir.fileName();
    e["size"] = dir.fileSize();
    count++;
  }
  doc["file_count"] = count;
  publishJsonDocToTopic(resultsTopic, doc, false);
}

bool pendingScan = false;

void handleCommandJson(const String& payload) {
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    lastRxType = "badjson";
    setStatusMessage("bad json cmd", 2500);
    consoleLog(CLOG_WARN, ("[RX] Bad JSON: " + payload).c_str());
    return;
  }
  // Accept both `command` and legacy `type`
  const char* command = doc["command"] | doc["type"] | "";
  lastRxType = String(command);
  consoleLog(CLOG_RX, ("[RX] command: " + String(command)).c_str());

  if (!strcmp(command, "status") || !strcmp(command, "heartbeat")) {
    publishHeaterStatus(false);
    setStatusMessage("publishing status", 1500);
    return;
  }
  if (!strcmp(command, "ls")) {
    pendingLs = true;
    setStatusMessage("ls queued", 1500);
    return;
  }
  if (!strcmp(command, "calibrate")) {
    if (!doc.containsKey("actual_power_watts") && !doc.containsKey("actualpowerwatts")) {
      setStatusMessage("cal missing watts", 2500);
      return;
    }
    pendingActualWatts = doc["actual_power_watts"] | doc["actualpowerwatts"] | 0.0f;
    pendingCalibration = true;
    setStatusMessage("cal queued", 1500);
    return;
  }
  if (!strcmp(command, "purge_calibration") || !strcmp(command, "purgecalibration")) {
    pendingPurge = true;
    setStatusMessage("purge queued", 1500);
    return;
  }
#ifdef SHARED_LIB_USE_ONEWIRE
  if (!strcmp(command, "scan") || !strcmp(command, "temps")) {
    pendingScan = true;
    setStatusMessage("scan queued", 1500);
    return;
  }
#endif
  if (doc.containsKey("power_percent") || doc.containsKey("powerpercent")) {
    int pct = doc["power_percent"] | doc["powerpercent"] | 0;
    updatePowerValues(pct);
    lastCommandMs = millis();
    setStatusMessage(("power " + String(requestedPowerPct) + "%").c_str(), 2000);
    publishHeaterStatus(false);
    return;
  }
  if (doc.containsKey("power")) {
    updatePowerValues(doc["power"].as<int>());
    lastCommandMs = millis();
    setStatusMessage(("power " + String(requestedPowerPct) + "%").c_str(), 2000);
    publishHeaterStatus(false);
    return;
  }
  setStatusMessage("cmd ignored", 1500);
  consoleLog(CLOG_WARN, ("[CMD] unknown: " + String(command)).c_str());
}

void serviceHeaterDeferred() {
  if (pendingLs)         { pendingLs = false;          publishFilesystemListing(); }
  if (pendingCalibration){ pendingCalibration = false; handleCalibrationRequest(pendingActualWatts); publishHeaterStatus(false); }
  if (pendingPurge)      { pendingPurge = false;       purgeCalibrationFile();     publishHeaterStatus(false); }
}
