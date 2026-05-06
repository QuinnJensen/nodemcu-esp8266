#pragma once
#include <Arduino.h>
#include <time.h>
#include <IPAddress.h>

String sanitizeTopicPart(const String& in);
String safeDeviceId();
String ipToString(IPAddress ip);
String timeToString(time_t t);
String currentTimestampString();
void setupTimeHelpers();
