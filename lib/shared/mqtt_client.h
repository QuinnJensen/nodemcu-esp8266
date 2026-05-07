#pragma once
#include <Arduino.h>
#include <PubSubClient.h>

using MqttMessageHandler  = void (*)(const String& topic, const String& payload);
using MqttConnectedHandler = void (*)();

struct MqttClientDisplay {
  void (*kickSpinner)(unsigned long durationMs) = nullptr;
  void (*setStatus)(const String& msg, unsigned long holdMs) = nullptr;
};

void setMqttClientDisplayCallbacks(const MqttClientDisplay& cb);
void setMqttMessageHandler(MqttMessageHandler handler);
void setMqttConnectedHandler(MqttConnectedHandler handler);

bool publishJsonDocToTopic(const char* topic, const JsonDocument& doc, bool retained);
bool mqttConnect();
void initMqttClient();
void startMqttIfWifiReady();
void serviceMqttClient();
