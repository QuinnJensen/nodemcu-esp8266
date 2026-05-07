// mqtt_publish.h
#pragma once
#include <stdint.h>

void publishAggregateStatus();
void publishPerSensorStatus(uint8_t i);
void publishPerSensorStatuses();
void publishWaterStatus();
void publishCommandResult(const char* type, bool ok, const char* msg);
void initialSampleAndPublish();
