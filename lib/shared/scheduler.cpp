// scheduler.cpp
#include "scheduler.h"
#include "app_state.h"
#include "app_config.h"
#include "sensor_bus.h"
#include "water_probe.h"
#include "mqtt_client.h"
#include "pins_and_constants.h"

void runScheduledTasks() {
  unsigned long now = millis();

  if (now - lastSensorHeartbeatMs >= sensorheartbeatintervalms) {
    Serial.println("sample sensors");
    sampleSensors();
    publishPerSensorStatuses(false);
    lastSensorHeartbeatMs = now;
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

  // NEW: refresh WiFi RSSI about once a second
  static unsigned long lastRssiUpdate = 0;
  if (WiFi.isConnected() && now - lastRssiUpdate >= 1000) {
    lastRssi = WiFi.RSSI();
    lastRssiUpdate = now;
  }
}
