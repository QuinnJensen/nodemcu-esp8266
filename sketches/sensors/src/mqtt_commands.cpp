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
    if (!strcmp(command, "scan") && !config.sensorNetworkEnabled) {
      publishCommandResult("scan", false, "Sensor network feature is disabled");
      setStatusMessage("scan disabled", 1500);
      consoleLog(CLOG_WARN, "[CMD] scan: failed because sensor network feature is disabled.");
      return;
    }

    if (config.sensorNetworkEnabled) {
      scanSensors(true);
      readTemperatures();
      lastSensorSampleMs = millis();
    }
    if (config.waterProbeEnabled) {
      beginWaterSample();
    }
    publishAggregateStatus();
    if (config.sensorNetworkEnabled) {
      publishPerSensorStatuses();
    }
    setStatusMessage("scanpublish", 1500);
    consoleLog(CLOG_INFO, "[CMD] scan/status: aggregate published.");
    return;
  }

  if (!strcmp(command, "water") || !strcmp(command, "waterstatus")) {
    if (!config.waterProbeEnabled) {
      publishCommandResult("water", false, "Water probe feature is disabled");
      setStatusMessage("water disabled", 1500);
      consoleLog(CLOG_WARN, "[CMD] water: failed because water probe feature is disabled.");
      return;
    }
    beginWaterSample();
    setStatusMessage("water queued", 1500);
    consoleLog(CLOG_INFO, "[CMD] water: probe sample queued.");
    return;
  }

  lastRxType = "command_ignored";
  setStatusMessage("cmd ignored", 1500);
  consoleLog(CLOG_WARN, ("[CMD] Ignored unknown command: " + String(command)).c_str());
}
