#include <ArduinoJson.h>
#include "app_state.h"
#include "app_config.h"
#include "sensor_bus.h"
#include "water_probe.h"
#include "mqtt_publish.h"
#include "util.h"
#include "display_ui.h"
#include "console_log.h"
#include "mqtt_commands.h"

void handleCommandJson(const String& payload) {
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    lastRxType = "badjson";
    setStatusMessage("bad json cmd", 2500);
    consoleLog(CLOG_WARN, ("[RX] Bad JSON: " + payload).c_str());
    return;
  }

  const char* command = doc["command"] | "";
  doc["type"] = "command";
  lastRxType = String(command);

  consoleLog(CLOG_RX, ("[RX] MQTT command: " + String(command)).c_str());

  if (!strcmp(command, "scan") || !strcmp(command, "status") || !strcmp(command, "heartbeat")) {
    scanSensors(true);
    readTemperatures();
    lastSensorSampleMs = millis();
    beginWaterSample();
    publishAggregateStatus();
    publishPerSensorStatuses();
    setStatusMessage("scanpublish", 1500);
    consoleLog(CLOG_INFO, "[CMD] scan/status: bus scanned, aggregate published.");
    return;
  }

  if (!strcmp(command, "water") || !strcmp(command, "waterstatus")) {
    beginWaterSample();
    setStatusMessage("water queued", 1500);
    consoleLog(CLOG_INFO, "[CMD] water: probe sample queued.");
    return;
  }

  lastRxType = "command_ignored";
  setStatusMessage("cmd ignored", 1500);
  consoleLog(CLOG_WARN, ("[CMD] Ignored unknown command: " + String(command)).c_str());
}
