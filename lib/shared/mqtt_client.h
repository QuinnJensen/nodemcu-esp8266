// mqtt_client.h
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

void initMqttClient();
void startMqttIfWifiReady();
void serviceMqttClient();
bool mqttConnect();
bool publishJsonDocToTopic(const char* topic, const JsonDocument& doc, bool retained = false);
void publishAggregateStatus(bool retained = false);
void publishPerSensorStatus(uint8_t i, bool retained = false);
void publishPerSensorStatuses(bool retained = false);
void publishWaterStatus(bool retained = false);
void mqttCallback(char* topic, byte* payload, unsigned int length);
void initialSampleAndPublish();
