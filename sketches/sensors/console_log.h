// console_log.h
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Entry type tags — also used as CSS class names in the web console.
#define CLOG_RX   "rx"    // incoming MQTT command received
#define CLOG_TX   "tx"    // outgoing MQTT publish
#define CLOG_INFO "info"  // general informational
#define CLOG_WARN "warn"  // warnings / soft errors
#define CLOG_ERR  "err"   // errors

void consoleLog(const char* type, const char* msg);
void consoleLog(const char* type, const String& msg);

// Serialise entries with seq > afterSeq into arr.
// Returns current highest seq so the caller can pass it next time.
uint32_t appendConsoleLogJson(JsonArray& arr, uint32_t afterSeq);
