// web_ui.cpp - shared web UI plumbing
#include "web_ui.h"
#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include "app_state.h"
#include "app_config.h"
#include "mqtt_client.h"
#include "util.h"
#include "console_log.h"
#include "display_ui.h"

static WebStatusJsonFn   sStatusFn  = nullptr;
static WebConfigJsonFn   sConfigFn  = nullptr;
static WebConsoleHelpFn  sHelpFn    = nullptr;
static WebExtraRoutesFn  sRoutesFn  = nullptr;
static WebDeferredFn     sDeferFn   = nullptr;

void setWebStatusJsonFn(WebStatusJsonFn fn)   { sStatusFn = fn; }
void setWebConfigJsonFn(WebConfigJsonFn fn)   { sConfigFn = fn; }
void setWebConsoleHelpFn(WebConsoleHelpFn fn) { sHelpFn   = fn; }
void setWebExtraRoutesFn(WebExtraRoutesFn fn) { sRoutesFn = fn; }
void setWebDeferredFn(WebDeferredFn fn)       { sDeferFn  = fn; }

void webSendJsonDoc(JsonDocument& doc, int code) {
  size_t len = measureJson(doc);
  webServer.setContentLength(len);
  webServer.send(code, "application/json", "");
  serializeJson(doc, webServer.client());
  yield();
}

void webSendOk(const char* msg) {
  StaticJsonDocument<192> doc;
  doc["ok"] = true;
  doc["message"] = msg ? msg : "ok";
  webSendJsonDoc(doc);
}

void webSendError(const char* msg, int code) {
  StaticJsonDocument<192> doc;
  doc["ok"] = false;
  doc["message"] = msg ? msg : "error";
  webSendJsonDoc(doc, code);
}

static bool streamLittleFsFile(const char* path, const char* contentType) {
  if (!LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  webServer.streamFile(f, contentType);
  f.close();
  yield();
  return true;
}

static void handleHomePage() {
  if (streamLittleFsFile("/index.html", "text/html; charset=utf-8")) return;
  webServer.send(500, "text/plain", "index.html missing from LittleFS");
}

static void handleApiStatus() {
  DynamicJsonDocument doc(2048);
  doc["id"]              = safeDeviceId();
  doc["ssid"]            = WiFi.SSID();
  doc["ip"]              = ipToString(WiFi.localIP());
  doc["wifi_connected"]  = (WiFi.status() == WL_CONNECTED);
  doc["mqtt_connected"]  = mqtt.connected();
  doc["rssidbm"]         = WiFi.RSSI();
  doc["freeheap"]        = ESP.getFreeHeap();
  doc["prometheusport"]  = config.prometheusPort;
  doc["uptime_ms"]       = millis() - bootMillis;
  doc["last_status"]     = lastStatusMsg;
  doc["last_rx_type"]    = lastRxType;
  doc["build_version"]   = buildVersion;
  doc["timezone"]        = config.timezone;
  String ts = currentTimestampString();
  if (ts.length()) doc["timestamp"] = ts;
  if (sStatusFn) sStatusFn(doc);
  webSendJsonDoc(doc);
}

static void handleApiConfig() {
  DynamicJsonDocument doc(2048);
  doc["mqtthost"]       = config.mqttHost;
  doc["mqttport"]       = config.mqttPort;
  doc["basetopic"]      = config.baseTopic;
  doc["deviceid"]       = safeDeviceId();
  doc["prometheusport"] = config.prometheusPort;
  doc["led_enabled"]    = config.ledEnabled;
  doc["timezone"]       = config.timezone;

  JsonObject topics = doc.createNestedObject("topics");
  topics["command"] = commandTopic;
  topics["status"]  = statusTopic;
  topics["results"] = resultsTopic;

  if (sConfigFn) sConfigFn(doc);
  webSendJsonDoc(doc);
}

static void handleApiFsList() {
  DynamicJsonDocument doc(2048);
  JsonArray files = doc.createNestedArray("files");
  FSInfo info;
  LittleFS.info(info);
  doc["total_bytes"] = info.totalBytes;
  doc["used_bytes"]  = info.usedBytes;
  Dir dir = LittleFS.openDir("/");
  while (dir.next()) {
    JsonObject f = files.createNestedObject();
    f["name"] = dir.fileName();
    f["size"] = dir.fileSize();
  }
  webSendJsonDoc(doc);
}

static void handleApiFsFile() {
  if (!webServer.hasArg("path")) { webSendError("missing path", 400); return; }
  String path = webServer.arg("path");
  if (!path.startsWith("/")) path = "/" + path;
  if (!LittleFS.exists(path)) { webSendError("file not found", 404); return; }
  File f = LittleFS.open(path, "r");
  if (!f) { webSendError("open failed", 500); return; }
  webServer.streamFile(f, "text/plain; charset=utf-8");
  f.close();
  yield();
}

static void handleApiConsoleLog() {
  uint32_t afterSeq = 0;
  if (webServer.hasArg("after"))
    afterSeq = (uint32_t)webServer.arg("after").toInt();
  DynamicJsonDocument doc(13312);
  JsonArray entries = doc.createNestedArray("entries");
  uint32_t latestSeq = appendConsoleLogJson(entries, afterSeq);
  doc["seq"] = latestSeq;
  webSendJsonDoc(doc);
}

static void handlePostConsoleCommand() {
  if (!webServer.hasArg("cmd")) { webSendError("missing cmd", 400); return; }
  String cmd = webServer.arg("cmd");
  cmd.trim();
  if (!cmd.length()) { webSendError("empty command", 400); return; }
  consoleLog(CLOG_RX, ("[console] " + cmd).c_str());
  if (cmd.startsWith("/")) {
    if (cmd.equalsIgnoreCase("/help")) {
      consoleLog(CLOG_INFO, "Console \xe2\x80\x94 available commands:");
      consoleLog(CLOG_INFO, "  /help   \xe2\x80\x94 show this message");
      consoleLog(CLOG_INFO, "  /status \xe2\x80\x94 log current node status");
      consoleLog(CLOG_INFO, "  /clear  \xe2\x80\x94 clear the console display (UI only)");
      if (sHelpFn) sHelpFn();
      consoleLog(CLOG_INFO, "Or paste a raw JSON command, e.g. {\"command\":\"status\"}");
      webSendOk("help sent");
      return;
    }
    if (cmd.equalsIgnoreCase("/status")) {
      consoleLog(CLOG_INFO, (String("[status] heap=") + ESP.getFreeHeap() +
                             " uptime=" + (millis()/1000) + "s" +
                             " mqtt=" + (mqtt.connected() ? "up" : "down") +
                             " wifi=" + (WiFi.status()==WL_CONNECTED ? "up" : "down")).c_str());
      webSendOk("status logged");
      return;
    }
    if (cmd.equalsIgnoreCase("/clear")) { webSendOk("clear"); return; }
    // Backward-compat: /word -> {"command":"word"}
    String word = cmd.substring(1);
    word.trim();
    if (word.length()) {
      webConsoleCommandPending = String("{\"command\":\"") + word + "\"}";
      webSendOk("command queued");
      return;
    }
    consoleLog(CLOG_WARN, ("Unknown command: " + cmd).c_str());
    webSendError(("Unknown command: " + cmd).c_str(), 400);
    return;
  }
  // Bare-word shorthand or raw JSON: queue for dispatch via shared handler
  if (cmd.indexOf('{') < 0 && cmd.indexOf(' ') < 0) {
    webConsoleCommandPending = String("{\"command\":\"") + cmd + "\"}";
    webSendOk("command queued");
    return;
  }
  webConsoleCommandPending = cmd;
  webSendOk("command queued");
}

static void handlePostServicesConfig() {
  bool changed = false;
  bool mqttChanged = false;
  if (webServer.hasArg("mqtthost"))       { bool c = setMqttHostValue(webServer.arg("mqtthost").c_str()); changed |= c; mqttChanged |= c; }
  if (webServer.hasArg("mqttport"))       { bool c = setMqttPortValue((uint16_t)webServer.arg("mqttport").toInt()); changed |= c; mqttChanged |= c; }
  if (webServer.hasArg("basetopic"))      { bool c = setBaseTopicValue(webServer.arg("basetopic").c_str()); changed |= c; mqttChanged |= c; }
  if (webServer.hasArg("deviceid"))       { bool c = setDeviceIdValue(webServer.arg("deviceid").c_str()); changed |= c; mqttChanged |= c; }
  if (webServer.hasArg("prometheusport")) changed |= setPrometheusPortValue((uint16_t)webServer.arg("prometheusport").toInt());
  
  saveConfig();
  if (mqttChanged) notifyMqttConfigChanged();
  
  setStatusMessage(changed ? "services saved" : "services unchanged", 1500);
  webSendOk("services saved");
}

static void handlePostTimeConfig() {
  if (!webServer.hasArg("timezone")) { webSendError("missing timezone", 400); return; }
  String tz = webServer.arg("timezone");
  tz.trim();
  if (!tz.length()) { webSendError("timezone empty", 400); return; }
  if (!setTimezoneValue(tz.c_str())) { webSendError("invalid timezone", 400); return; }
  saveConfig();
  setStatusMessage("tz saved", 1500);
  consoleLog(CLOG_INFO, (String("[cfg] timezone -> ") + config.timezone).c_str());
  webSendOk("timezone saved");
}

static void handlePostDisplayConfig() {
  if (!webServer.hasArg("led_enabled")) { webSendError("missing led_enabled", 400); return; }
  String val = webServer.arg("led_enabled");
  bool enabled = (val == "1" || val.equalsIgnoreCase("true"));
  setLedEnabled(enabled);
  saveConfig();
  setStatusMessage(enabled ? "LED enabled" : "LED disabled", 1500);
  webSendOk(enabled ? "LED enabled" : "LED disabled");
}

void startMainWebUi() {
  webServer.on("/",           HTTP_GET, handleHomePage);
  webServer.on("/index.html", HTTP_GET, handleHomePage);
  webServer.on("/api/status",          HTTP_GET,  handleApiStatus);
  webServer.on("/api/config",          HTTP_GET,  handleApiConfig);
  webServer.on("/api/fs/list",         HTTP_GET,  handleApiFsList);
  webServer.on("/api/fs/file",         HTTP_GET,  handleApiFsFile);
  webServer.on("/api/console/log",     HTTP_GET,  handleApiConsoleLog);
  webServer.on("/api/console/command", HTTP_POST, handlePostConsoleCommand);
  webServer.on("/api/config/services", HTTP_POST, handlePostServicesConfig);
  webServer.on("/api/config/display",  HTTP_POST, handlePostDisplayConfig);
  webServer.on("/api/config/time",     HTTP_POST, handlePostTimeConfig);
  if (sRoutesFn) sRoutesFn();
  webServer.onNotFound([]() {
    if (webServer.uri().startsWith("/api/")) { webSendError("not found", 404); return; }
    if (LittleFS.exists(webServer.uri())) {
      String path = webServer.uri();
      String type = "text/plain";
      if      (path.endsWith(".css"))  type = "text/css; charset=utf-8";
      else if (path.endsWith(".js"))   type = "application/javascript; charset=utf-8";
      else if (path.endsWith(".html")) type = "text/html; charset=utf-8";
      else if (path.endsWith(".svg"))  type = "image/svg+xml";
      else if (path.endsWith(".json")) type = "application/json";
      File f = LittleFS.open(path, "r");
      if (f) { webServer.streamFile(f, type); f.close(); return; }
    }
    handleHomePage();
  });
  webServer.begin();
}

void serviceMainWebUi() { webServer.handleClient(); }

void serviceDeferredWebActions() {
  if (sDeferFn) sDeferFn();
  if (webConsoleCommandPending.length()) {
    String cmd = webConsoleCommandPending;
    webConsoleCommandPending = "";
    extern void handleCommandJson(const String&);  // sketch must define
    handleCommandJson(cmd);
  }
}
