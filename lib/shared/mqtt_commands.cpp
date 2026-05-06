#include <ArduinoJson.h>
#include "app_state.h"
#include "app_config.h"
#include "sensor_bus.h"
#include "water_probe.h"
#include "mqtt_client.h"
#include "util.h"
#include "display_ui.h"
#include "mqtt_commands.h"

void handleCommandJson(const String& payload) {
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    lastRxType = "badjson";
    setStatusMessage("bad json cmd", 2500);
    return;
  }

  const char* command = doc["command"] | "";
  doc["type"] = "command";
  lastRxType = String(command);

  if (!strcmp(command, "scan") || !strcmp(command, "status") || !strcmp(command, "heartbeat")) {
    scanSensors(true);
    readTemperatures();
    lastSensorSampleMs = millis();
    sampleWaterLevel();
    publishAggregateStatus(false);
    publishPerSensorStatuses(false);
    publishWaterStatus(false);
    setStatusMessage("scanpublish", 1500);
    return;
  }

  if (!strcmp(command, "water") || !strcmp(command, "waterstatus")) {
    sampleWaterLevel();
    publishAggregateStatus(false);
    publishWaterStatus(false);
    setStatusMessage("water publish", 1500);
    return;
  }

  lastRxType = "command_ignored";
  setStatusMessage("cmd ignored", 1500);
}
