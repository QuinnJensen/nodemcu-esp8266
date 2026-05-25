// console_log.cpp
#include "console_log.h"
#include <time.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <stdarg.h>
#include "util.h"

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
static uint8_t   clogHead = 0;
static uint32_t  clogSeq  = 0;

static WiFiUDP udp;
static const uint16_t udpPort = 5555;

static void sendUdp(const char* type, const char* msg, bool newline) {
  if (WiFi.status() == WL_CONNECTED) {
    // Broadcast to the subnet
    IPAddress localIp = WiFi.localIP();
    IPAddress broadcastIp = localIp;
    broadcastIp[3] = 255; 
    
    udp.beginPacket(broadcastIp, udpPort);
    
    // Prefix: [id:ip:type] or [id:ip]
    udp.write("[");
    udp.write(safeDeviceId().c_str());
    udp.write(":");
    udp.write(ipToString(localIp).c_str());
    if (type && type[0]) {
      udp.write(":");
      udp.write(type);
    }
    udp.write("] ");
    
    udp.write(msg);
    if (newline) udp.write("\n");
    udp.endPacket();
  }
}

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

  // Mirror to Serial
  Serial.print("["); Serial.print(e.type); Serial.print("] ");
  Serial.println(e.msg);
  
  // Mirror to UDP with context prefix
  sendUdp(e.type, e.msg, true);
}

void consoleLog(const char* type, const String& msg) {
  consoleLog(type, msg.c_str());
}

void remotePrint(const String& msg) {
  Serial.print(msg);
  sendUdp(nullptr, msg.c_str(), false);
}

void remotePrintln(const String& msg) {
  Serial.println(msg);
  sendUdp(nullptr, msg.c_str(), true);
}

void remotePrintf(const char* format, ...) {
  char buf[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  Serial.print(buf);
  sendUdp(nullptr, buf, false);
}

uint32_t appendConsoleLogJson(JsonArray& arr, uint32_t afterSeq) {
  for (uint8_t n = 0; n < CLOG_CAP; n++) {
    uint8_t i = (clogHead + n) % CLOG_CAP;
    if (clogBuf[i].seq == 0)         continue;
    if (clogBuf[i].seq <= afterSeq)  continue;
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
