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

  // Phase 2: collect temperature results 800ms after conversion was requested
  if (waitingToCollect && conversionPending &&
      now - conversionRequestedMs >= 800) {
    collectTemperatureResults();
    publishPerSensorStatuses(false);
    waitingToCollect = false;
  }

  // Sensor heartbeat: scan + fire async conversion
  if (now - lastSensorHeartbeatMs >= sensorheartbeatintervalms) {
    Serial.println("sample sensors");
    scanSensors();
    requestTemperatureConversion();
    waitingToCollect = true;
    lastSensorHeartbeatMs = now;
    // publish happens in the collect phase above, 800ms later
  }

  if (now - lastWaterHeartbeatMs >= config.waterHeartbeatIntervalMs) {
    Serial.println("sample water level");
    sampleWaterLevel();
    publishWaterStatus(false);
    lastWaterHeartbeatMs = now;
  }

  if (now - lastAggregateHeartbeatMs >= aggregateheartbeatintervalms) {
    Serial.println("heartbeat");
    publishAggregateStatus(false);
    lastAggregateHeartbeatMs = now;
  }

  static unsigned long lastRssiUpdate = 0;
  if (WiFi.isConnected() && now - lastRssiUpdate >= 1000) {
    lastRssi = WiFi.RSSI();
    lastRssiUpdate = now;
  }
}
