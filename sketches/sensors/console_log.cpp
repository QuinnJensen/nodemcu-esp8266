// console_log.cpp
#include "console_log.h"
#include <time.h>

// Ring-buffer entries are bumped to 256 chars so MQTT publish lines can hold
// the full topic + small JSON payloads. Heap impact: 32 entries * 256 chars
// + overhead = ~8.5 KB static, well within ESP8266 budget. Cap was reduced
// from 48 -> 32 to keep total footprint roughly constant.
#define CLOG_CAP     32
#define CLOG_MSG_LEN 256

struct CLogEntry {
  uint32_t seq;
  uint32_t uptime_ms;
  uint32_t epoch_s;          // 0 if wall-clock not yet configured
  char     local_hms[9];     // "HH:MM:SS" in device-configured TZ; empty if epoch_s==0
  char     type[8];
  char     msg[CLOG_MSG_LEN];
};

static CLogEntry clogBuf[CLOG_CAP];
static uint8_t   clogHead = 0;   // next write slot (wraps)
static uint32_t  clogSeq  = 0;   // monotonic counter, never resets

void consoleLog(const char* type, const char* msg) {
  CLogEntry& e = clogBuf[clogHead];
  e.seq       = ++clogSeq;
  e.uptime_ms = millis();
  time_t now  = time(nullptr);
  if (now > 100000) {
    e.epoch_s = (uint32_t)now;
    struct tm tmStruct;
    localtime_r(&now, &tmStruct);
    strftime(e.local_hms, sizeof(e.local_hms), "%H:%M:%S", &tmStruct);
  } else {
    e.epoch_s = 0;
    e.local_hms[0] = '\0';
  }
  strncpy(e.type, type ? type : "info", sizeof(e.type) - 1);
  e.type[sizeof(e.type) - 1] = '\0';
  strncpy(e.msg, msg ? msg : "", sizeof(e.msg) - 1);
  e.msg[sizeof(e.msg) - 1] = '\0';
  clogHead = (clogHead + 1) % CLOG_CAP;
}

void consoleLog(const char* type, const String& msg) {
  consoleLog(type, msg.c_str());
}

// Walk oldest-first (head is the next-write slot, so it points to the
// oldest entry when the buffer is full).
uint32_t appendConsoleLogJson(JsonArray& arr, uint32_t afterSeq) {
  for (uint8_t n = 0; n < CLOG_CAP; n++) {
    uint8_t i = (clogHead + n) % CLOG_CAP;
    if (clogBuf[i].seq == 0)         continue;  // slot never written
    if (clogBuf[i].seq <= afterSeq)  continue;  // client already has it
    JsonObject o = arr.createNestedObject();
    o["seq"]       = clogBuf[i].seq;
    o["uptime_ms"] = clogBuf[i].uptime_ms;
    if (clogBuf[i].epoch_s) {
      o["epoch_s"] = clogBuf[i].epoch_s;
      o["local"]   = clogBuf[i].local_hms;
    }
    o["type"]      = clogBuf[i].type;
    o["msg"]       = clogBuf[i].msg;
  }
  return clogSeq;
}
