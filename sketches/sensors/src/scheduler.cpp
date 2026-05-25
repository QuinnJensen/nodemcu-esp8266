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

  // Phase 2: collect temperature results 1000ms after conversion was requested
  if (waitingToCollect && conversionPending &&
      now - conversionRequestedMs >= 1000) {
    collectTemperatureResults();
    publishPerSensorStatuses();
    waitingToCollect = false;
  }

  // Sensor heartbeat: scan + fire async conversion
  // Delay if water probe is currently active or we are waiting for a conversion
  if (now - lastSensorHeartbeatMs >= sensorheartbeatintervalms && !waterProbing && !waitingToCollect) {
    // Alternate between scanning and sampling to avoid bus noise during conversion
    static bool alternateScan = true;
    if (alternateScan) {
      Serial.println("[1-WIRE] Periodic bus rescan");
      scanSensors();
    } else {
      Serial.println("[1-WIRE] Triggering conversion");
      requestTemperatureConversion();
      waitingToCollect = true;
    }
    alternateScan = !alternateScan;
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
