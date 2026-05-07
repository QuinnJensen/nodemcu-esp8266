// web_ui.cpp
#include "web_ui.h"

#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>

#include "app_state.h"
#include "app_config.h"
#include "sensor_bus.h"
#include "sensor_names.h"
#include "water_probe.h"
#include "mqtt_client.h"
#include "metrics_server.h"
#include "util.h"
#include "display_ui.h"
#include "console_log.h"
#include "mqtt_commands.h"

static void sendJsonDoc(JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  webServer.send(200, "application/json", out);
  yield();
}

static void sendOk(const char* msg = "ok") {
  StaticJsonDocument<192> doc;
  doc["ok"] = true;
  doc["message"] = msg;
  sendJsonDoc(doc);
}

static void sendError(const char* msg, int code = 400) {
  StaticJsonDocument<192> doc;
  doc["ok"] = false;
  doc["message"] = msg ? msg : "error";
  String out;
  serializeJson(doc, out);
  webServer.send(code, "application/json", out);
  yield();
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

void handleHomePage() {
  if (streamLittleFsFile("/index.html", "text/html; charset=utf-8")) return;
  webServer.send(500, "text/plain", "index.html missing from LittleFS");
}

void handleApiStatus() {
  StaticJsonDocument<1024> doc;
  doc["id"]              = safeDeviceId();
  doc["ssid"]            = WiFi.SSID();
  doc["ip"]              = ipToString(WiFi.localIP());
  doc["wifi_connected"]  = (WiFi.status() == WL_CONNECTED);
  doc["mqtt_connected"]  = mqtt.connected();
  doc["rssidbm"]         = WiFi.RSSI();
  doc["freeheap"]        = ESP.getFreeHeap();
  doc["sensorcount"]     = sensorCount;
  doc["simulated"]       = useFakeSensors;
  doc["networkdetected"] = sensorNetworkDetected;
  doc["prometheusport"]  = config.prometheusPort;
  doc["uptime_ms"]       = millis() - bootMillis;
  doc["last_status"]     = lastStatusMsg;
  doc["last_rx_type"]    = lastRxType;
  doc["build_version"]   = buildVersion;
  doc["timezone"]        = config.timezone;
  String ts = currentTimestampString();
  if (ts.length()) doc["timestamp"] = ts;
  appendWaterToJson(doc);
  sendJsonDoc(doc);
}

void handleApiTemps() {
  StaticJsonDocument<1536> doc;
  doc["sensorcount"]        = sensorCount;
  doc["simulated"]          = useFakeSensors;
  doc["networkdetected"]    = sensorNetworkDetected;
  doc["last_sample_ms_age"] = lastSensorSampleMs > 0 ? (millis() - lastSensorSampleMs) : 0;
  doc["last_rescan_ms_age"] = lastSensorRescanMs > 0 ? (millis() - lastSensorRescanMs) : 0;

  JsonArray sensors = doc.createNestedArray("sensors");
  for (uint8_t i = 0; i < sensorCount; i++) {
    JsonObject s = sensors.createNestedObject();
    s["index"]     = i + 1;
    s["name"]      = sensorNames[i];
    s["address"]   = sensorAddressString(i);
    s["connected"] = sensorPresent[i];
    if (isnan(sensorTempsC[i])) s["tempc"] = nullptr;
    else s["tempc"] = sensorTempsC[i];
  }
  sendJsonDoc(doc);
}

void handleApiWater() {
  StaticJsonDocument<512> doc;
  appendWaterToJson(doc);
  sendJsonDoc(doc);
}

void handleApiConfig() {
  StaticJsonDocument<1024> doc;
  doc["mqtthost"]       = config.mqttHost;
  doc["mqttport"]       = config.mqttPort;
  doc["basetopic"]      = config.baseTopic;
  doc["deviceid"]       = safeDeviceId();
  doc["prometheusport"] = config.prometheusPort;
  doc["led_enabled"]    = config.ledEnabled;
  doc["timezone"]       = config.timezone;

  JsonObject water = doc.createNestedObject("water");
  water["intervalms"] = config.waterHeartbeatIntervalMs;
  JsonArray thresholds = water.createNestedArray("thresholds");
  for (uint8_t i = 0; i < waterthresholdcount; i++) thresholds.add(config.waterThresholds[i]);

  JsonObject topics = doc.createNestedObject("topics");
  topics["command"] = commandTopic;
  topics["status"]  = statusTopic;
  topics["results"] = resultsTopic;
  topics["water"]   = waterTopic;

  sendJsonDoc(doc);
}

// GET /api/fs/list
void handleApiFsList() {
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
  sendJsonDoc(doc);
}

// GET /api/fs/file?path=/foo.txt
void handleApiFsFile() {
  if (!webServer.hasArg("path")) { sendError("missing path"); return; }
  String path = webServer.arg("path");
  if (!path.startsWith("/")) path = "/" + path;
  if (!LittleFS.exists(path)) { sendError("file not found", 404); return; }
  File f = LittleFS.open(path, "r");
  if (!f) { sendError("open failed", 500); return; }
  webServer.streamFile(f, "text/plain; charset=utf-8");
  f.close();
  yield();
}

// ── Console API ──────────────────────────────────────────────────────────────

// GET /api/console/log?after=<seq>
void handleApiConsoleLog() {
  uint32_t afterSeq = 0;
  if (webServer.hasArg("after"))
    afterSeq = (uint32_t)webServer.arg("after").toInt();

  // 32 ring entries * (uptime_ms + epoch_s + local HH:MM:SS + 256-char msg + seq + type)
  // serializes to ~12 KB worst-case. ESP8266 free heap at idle is ~25 KB so this
  // is safe; pass a custom Allocator if heap pressure becomes a problem.
  DynamicJsonDocument doc(13312);
  JsonArray entries = doc.createNestedArray("entries");
  uint32_t latestSeq = appendConsoleLogJson(entries, afterSeq);
  doc["seq"] = latestSeq;
  sendJsonDoc(doc);
}

// Known bare-word commands that map to the MQTT command JSON dispatcher.
// Keeping the list explicit (instead of pass-through) so unknown words still
// produce a clear "unknown" reply without silently building bogus JSON.
static bool isShorthandCommand(const String& word) {
  return word.equalsIgnoreCase("scan")
      || word.equalsIgnoreCase("status")
      || word.equalsIgnoreCase("heartbeat")
      || word.equalsIgnoreCase("water")
      || word.equalsIgnoreCase("waterstatus");
}

// POST /api/console/command  body: cmd=<text>
// Accepts:
//   /help, /status, /clear      -- handled here (UI/info only)
//   bare word (scan, water, ..) -- expanded to {"command":"<word>"}
//   raw JSON                    -- passed straight to handleCommandJson()
// In every case the payload that ends up dispatched goes through
// handleCommandJson() so behavior stays centralized with the MQTT path.
void handlePostConsoleCommand() {
  if (!webServer.hasArg("cmd")) { sendError("missing cmd"); return; }
  String cmd = webServer.arg("cmd");
  cmd.trim();
  if (!cmd.length()) { sendError("empty command"); return; }

  consoleLog(CLOG_RX, ("[console] " + cmd).c_str());

  if (cmd.startsWith("/")) {
    if (cmd.equalsIgnoreCase("/help")) {
      consoleLog(CLOG_INFO, "Sensor Node Console \xe2\x80\x94 available commands:");
      consoleLog(CLOG_INFO, "  /help    \xe2\x80\x94 show this message");
      consoleLog(CLOG_INFO, "  /status  \xe2\x80\x94 log current node status");
      consoleLog(CLOG_INFO, "  /clear   \xe2\x80\x94 clear the console display (UI only)");
      consoleLog(CLOG_INFO, "Shorthand commands (run via the MQTT command handler):");
      consoleLog(CLOG_INFO, "  scan        \xe2\x80\x94 scan bus + publish all sensor data");
      consoleLog(CLOG_INFO, "  status      \xe2\x80\x94 alias for scan");
      consoleLog(CLOG_INFO, "  heartbeat   \xe2\x80\x94 alias for scan");
      consoleLog(CLOG_INFO, "  water       \xe2\x80\x94 trigger water probe + publish");
      consoleLog(CLOG_INFO, "  waterstatus \xe2\x80\x94 alias for water");
      consoleLog(CLOG_INFO, "Or paste the raw JSON form, e.g. {\"command\":\"scan\"}.");
      sendOk("help sent");
      return;
    }
    if (cmd.equalsIgnoreCase("/status")) {
      consoleLog(CLOG_INFO, (String("[status] heap=") + ESP.getFreeHeap() +
                             " uptime=" + (millis()/1000) + "s" +
                             " mqtt=" + (mqtt.connected() ? "up" : "down") +
                             " wifi=" + (WiFi.status()==WL_CONNECTED ? "up" : "down")).c_str());
      sendOk("status logged");
      return;
    }
    if (cmd.equalsIgnoreCase("/clear")) {
      // Handled entirely in the browser; firmware just acks.
      sendOk("clear");
      return;
    }
    // Backwards-compat: /scan and /water still work, but they expand to the
    // shorthand form so they take the same path everything else does.
    if (cmd.equalsIgnoreCase("/scan") || cmd.equalsIgnoreCase("/water")) {
      String word = cmd.substring(1);
      webConsoleCommandPending = String("{\"command\":\"") + word + "\"}";
      sendOk("command queued");
      return;
    }
    consoleLog(CLOG_WARN, ("Unknown command: " + cmd).c_str());
    sendError(("Unknown command: " + cmd).c_str());
    return;
  }

  // Bare-word shorthand: expand into the JSON form and dispatch through the
  // shared MQTT command handler so behavior stays identical to the wire form.
  if (cmd.indexOf('{') < 0 && cmd.indexOf(' ') < 0 && isShorthandCommand(cmd)) {
    webConsoleCommandPending = String("{\"command\":\"") + cmd + "\"}";
    sendOk("command queued");
    return;
  }

  // Raw JSON -> deferred dispatch via handleCommandJson()
  webConsoleCommandPending = cmd;
  sendOk("command queued");
}

void handleApiScanSensors() {
  webRequestSensorScan = true;
  setStatusMessage("scan queued", 1200);
  sendOk("scan queued");
}

void handleApiSampleWater() {
  webRequestWaterSample = true;
  setStatusMessage("water queued", 1200);
  sendOk("water sample queued");
}

void handlePostServicesConfig() {
  bool changed = false;
  if (webServer.hasArg("mqtthost"))       changed |= setMqttHostValue(webServer.arg("mqtthost").c_str());
  if (webServer.hasArg("mqttport"))       changed |= setMqttPortValue((uint16_t)webServer.arg("mqttport").toInt());
  if (webServer.hasArg("basetopic"))      changed |= setBaseTopicValue(webServer.arg("basetopic").c_str());
  if (webServer.hasArg("deviceid"))       changed |= setDeviceIdValue(webServer.arg("deviceid").c_str());
  if (webServer.hasArg("prometheusport")) changed |= setPrometheusPortValue((uint16_t)webServer.arg("prometheusport").toInt());
  saveConfig();
  setStatusMessage(changed ? "services saved" : "services unchanged", 1500);
  sendOk("services saved");
}

void handlePostWaterConfig() {
  bool ok = true;
  if (webServer.hasArg("intervalms"))
    ok &= setWaterIntervalMs((uint32_t)webServer.arg("intervalms").toInt());

  uint16_t vals[waterthresholdcount];
  const char* keys[waterthresholdcount] = {"t0","t1","t2","t3","t4"};
  bool haveAll = true;
  for (uint8_t i = 0; i < waterthresholdcount; i++) {
    if (!webServer.hasArg(keys[i])) { haveAll = false; break; }
    vals[i] = (uint16_t)webServer.arg(keys[i]).toInt();
  }
  if (haveAll) ok &= setWaterThresholdsArray(vals, waterthresholdcount);
  if (!ok) { sendError("invalid water config"); return; }
  saveConfig();
  setStatusMessage("water cfg saved", 1500);
  sendOk("water config saved");
}

void handlePostSensorRename() {
  if (!webServer.hasArg("index") || !webServer.hasArg("name")) {
    sendError("missing index or name"); return;
  }
  uint8_t index1 = (uint8_t)webServer.arg("index").toInt();
  String name = webServer.arg("name");
  name.trim();
  if (!setSensorNameByIndex(index1, name.c_str())) { sendError("rename failed"); return; }
  setStatusMessage("sensor renamed", 1500);
  sendOk("sensor renamed");
}

void handlePostTimeConfig() {
  if (!webServer.hasArg("timezone")) { sendError("missing timezone"); return; }
  String tz = webServer.arg("timezone");
  tz.trim();
  if (!tz.length()) { sendError("timezone empty"); return; }
  if (!setTimezoneValue(tz.c_str())) { sendError("invalid timezone"); return; }
  saveConfig();
  setStatusMessage("tz saved", 1500);
  consoleLog(CLOG_INFO, (String("[cfg] timezone -> ") + config.timezone).c_str());
  sendOk("timezone saved");
}

void handlePostDisplayConfig() {
  if (!webServer.hasArg("led_enabled")) { sendError("missing led_enabled"); return; }
  String val = webServer.arg("led_enabled");
  bool enabled = (val == "1" || val.equalsIgnoreCase("true"));
  setLedEnabled(enabled);
  saveConfig();
  setStatusMessage(enabled ? "LED enabled" : "LED disabled", 1500);
  sendOk(enabled ? "LED enabled" : "LED disabled");
}

void startMainWebUi() {
  webServer.on("/",           HTTP_GET, handleHomePage);
  webServer.on("/index.html", HTTP_GET, handleHomePage);

  webServer.on("/api/status",          HTTP_GET,  handleApiStatus);
  webServer.on("/api/temps",           HTTP_GET,  handleApiTemps);
  webServer.on("/api/water",           HTTP_GET,  handleApiWater);
  webServer.on("/api/config",          HTTP_GET,  handleApiConfig);
  webServer.on("/api/fs/list",         HTTP_GET,  handleApiFsList);
  webServer.on("/api/fs/file",         HTTP_GET,  handleApiFsFile);
  webServer.on("/api/console/log",     HTTP_GET,  handleApiConsoleLog);

  webServer.on("/api/sensors/scan",    HTTP_POST, handleApiScanSensors);
  webServer.on("/api/water/sample",    HTTP_POST, handleApiSampleWater);
  webServer.on("/api/config/services", HTTP_POST, handlePostServicesConfig);
  webServer.on("/api/config/water",    HTTP_POST, handlePostWaterConfig);
  webServer.on("/api/sensors/rename",  HTTP_POST, handlePostSensorRename);
  webServer.on("/api/config/display",  HTTP_POST, handlePostDisplayConfig);
  webServer.on("/api/config/time",     HTTP_POST, handlePostTimeConfig);
  webServer.on("/api/console/command", HTTP_POST, handlePostConsoleCommand);

  webServer.onNotFound([]() {
    if (webServer.uri().startsWith("/api/")) { sendError("not found", 404); return; }
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

void serviceMainWebUi()  { webServer.handleClient(); }

void serviceDeferredWebActions() {
  if (webRequestSensorScan) {
    webRequestSensorScan = false;
    setStatusMessage("scan running", 1200);
    scanSensors(true);
    yield();
    readTemperatures();
    lastSensorSampleMs = millis();
    yield();
  }
  if (webRequestWaterSample) {
    webRequestWaterSample = false;
    beginWaterSample();
  }
  if (webConsoleCommandPending.length()) {
    String cmd = webConsoleCommandPending;
    webConsoleCommandPending = "";
    handleCommandJson(cmd);
  }
}
