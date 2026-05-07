// console_log.cpp
#include "console_log.h"

#define CLOG_CAP     48    // ring-buffer capacity (entries)
#define CLOG_MSG_LEN 120   // max characters per message

struct CLogEntry {
  uint32_t seq;
  uint32_t uptime_ms;
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
    o["type"]      = clogBuf[i].type;
    o["msg"]       = clogBuf[i].msg;
  }
  return clogSeq;
}
