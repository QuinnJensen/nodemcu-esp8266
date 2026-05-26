// scheduler.cpp
#include "scheduler.h"
#include "app_state.h"
#include "app_config.h"
#include "sensor_bus.h"
#include "water_probe.h"
#include "mqtt_publish.h"
#include "pins_and_constants.h"
#include "console_log.h"

// Two-phase async sensor read state
static bool waitingToCollect = false;

void runScheduledTasks() {
  unsigned long now = millis();

  // Drive the non-blocking water probe state machine every tick
  updateWaterSample();

  // Sensor heartbeat: scan + read temperatures
  // Delay if water probe is currently active to prevent timing conflicts
  if (now - lastSensorHeartbeatMs >= sensorheartbeatintervalms && !waterProbing) {
    Serial.println("sample sensors");
    scanSensors();
    readTemperatures();
    publishPerSensorStatuses();
    lastSensorHeartbeatMs = now;
  }

  // Water heartbeat: kick off a new (non-blocking) sample
  if (now - lastWaterHeartbeatMs >= config.waterHeartbeatIntervalMs) {
    remotePrintln("sample water level");
    beginWaterSample();
    lastWaterHeartbeatMs = now;
    // publishWaterStatus() is called inside updateWaterSample() on completion
  }

  if (now - lastAggregateHeartbeatMs >= aggregateheartbeatintervalms) {
    remotePrintln("heartbeat");
    publishAggregateStatus();
    lastAggregateHeartbeatMs = now;
  }

  static unsigned long lastSensorScroll = 0;
  if (sensorCount > 2 && now - lastSensorScroll >= 3000) {
    displayStartSensor = (displayStartSensor + 2) % sensorCount;
    lastSensorScroll = now;
  }

  static unsigned long lastRssiUpdate = 0;
  if (WiFi.isConnected() && now - lastRssiUpdate >= 1000) {
    lastRssi = WiFi.RSSI();
    lastRssiUpdate = now;
  }
}
