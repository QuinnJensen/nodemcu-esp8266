// scheduler.cpp
#include "scheduler.h"
#include "app_state.h"
#include "app_config.h"
#include "sensor_bus.h"
#include "water_probe.h"
#include "mqtt_publish.h"
#include "pins_and_constants.h"

// Two-phase async sensor read state
static bool waitingToCollect = false;

void runScheduledTasks() {
  unsigned long now = millis();

  // Drive the non-blocking water probe state machine every tick
  updateWaterSample();

  // Phase 2: collect temperature results 800ms after conversion was requested
  if (waitingToCollect && conversionPending &&
      now - conversionRequestedMs >= 800) {
    collectTemperatureResults();
    publishPerSensorStatuses();
    waitingToCollect = false;
  }

  // Sensor heartbeat: scan + fire async conversion
  // Delay if water probe is currently active to prevent LED/timing conflicts
  if (now - lastSensorHeartbeatMs >= sensorheartbeatintervalms && !waterProbing) {
    Serial.println("sample sensors");
    scanSensors();
    requestTemperatureConversion();
    waitingToCollect = true;
    lastSensorHeartbeatMs = now;
  }

  // Water heartbeat: kick off a new (non-blocking) sample
  if (now - lastWaterHeartbeatMs >= config.waterHeartbeatIntervalMs) {
    Serial.println("sample water level");
    beginWaterSample();
    lastWaterHeartbeatMs = now;
    // publishWaterStatus() is called inside updateWaterSample() on completion
  }

  if (now - lastAggregateHeartbeatMs >= aggregateheartbeatintervalms) {
    Serial.println("heartbeat");
    publishAggregateStatus();
    lastAggregateHeartbeatMs = now;
  }

  static unsigned long lastRssiUpdate = 0;
  if (WiFi.isConnected() && now - lastRssiUpdate >= 1000) {
    lastRssi = WiFi.RSSI();
    lastRssiUpdate = now;
  }
}
