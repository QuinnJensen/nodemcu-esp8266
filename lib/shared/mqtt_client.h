// mqtt_client.h
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Optional display callbacks — register before initMqttClient()
struct MqttClientDisplay {
  void (*kickSpinner)(unsigned long durationMs) = nullptr;
  void (*setStatus)(const String& msg, unsigned long holdMs) = nullptr;
};

void setMqttClientDisplayCallbacks(const MqttClientDisplay& cb);

void initMqttClient();
void startMqttIfWifiReady();
void serviceMqttClient();
void initialSampleAndPublish();

bool publishJsonDocToTopic(const char* topic, const JsonDocument& doc, bool retained = false);
void publishAggregateStatus(bool retained = false);
void publishPerSensorStatuses(bool retained = false);
void publishPerSensorStatus(uint8_t i, bool retained = false);
void publishWaterStatus(bool retained = false);
void publishCommandResult(const char* type, bool ok, const char* msg = nullptr);
bool mqttConnect();
