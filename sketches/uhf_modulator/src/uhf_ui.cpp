// uhf_ui.cpp — UI hooks for uhf_modulator
#include <ESP8266WiFi.h>
#include <math.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "app_state.h"
#include "app_config.h"
#include "console_log.h"
#include "display_ui.h"
#include "metrics_server.h"
#include "web_ui.h"
#include "util.h"
#include "uhf_codec.h"
#include "uhf_mqtt.h"
#ifdef SHARED_LIB_USE_ONEWIRE
#include "sensor_bus.h"
#include "sensor_names.h"
#endif

// ── Display body ───────────────────────────────────────────────────────────
static void uhfBody() {
  display.setCursor(0, 18);
  display.print("Profiles ");
  display.print(profileCount);
  display.setCursor(72, 18);
  display.print("Codes ");
  display.print(codeCount);

  display.setCursor(0, 28);
  display.print("LastTX ");
  String lastId(lastTxId);
  if (lastId.length() > 12) lastId = lastId.substring(0, 12);
  display.print(lastId);

  display.setCursor(0, 38);
  display.print(txBusy ? "TX..." : "idle");
  display.setCursor(48, 38);
  display.print("Cnt ");
  display.print(txCount);

#ifdef SHARED_LIB_USE_ONEWIRE
  if (sensorCount > 0) {
    display.setCursor(0, 48);
    uint8_t idx = 0;
    String name = sensorNames[idx][0] ? String(sensorNames[idx]) : String("S1");
    if (name.length() > 6) name = name.substring(0, 6);
    display.print(name);
    display.print(" ");
    if (!isnan(sensorTempsC[idx])) {
      display.print(String(sensorTempsC[idx] * 9.0f / 5.0f + 32.0f, 1));
      display.print("F");
    } else display.print("disc");
  }
#endif
}

// ── Metrics extras ─────────────────────────────────────────────────────────
static void uhfMetricsExtra(String& m) {
  String idLabel = prometheusEscaped(safeDeviceId());
  m += "# HELP uhf_profile_count Profile count.\n";
  m += "# TYPE uhf_profile_count gauge\nuhf_profile_count{id=\"" + idLabel + "\"} " + String(profileCount) + "\n";
  m += "# HELP uhf_code_count Code count.\n";
  m += "# TYPE uhf_code_count gauge\nuhf_code_count{id=\"" + idLabel + "\"} " + String(codeCount) + "\n";
  m += "# HELP uhf_tx_total Total RF transmissions.\n";
  m += "# TYPE uhf_tx_total counter\nuhf_tx_total{id=\"" + idLabel + "\"} " + String(txCount) + "\n";
  m += "# HELP uhf_tx_busy 1 while a transmission is in progress.\n";
  m += "# TYPE uhf_tx_busy gauge\nuhf_tx_busy{id=\"" + idLabel + "\"} " + String(txBusy ? 1 : 0) + "\n";
#ifdef SHARED_LIB_USE_ONEWIRE
  m += "# HELP uhf_sensor_count Number of active 1-Wire sensors.\n";
  m += "# TYPE uhf_sensor_count gauge\nuhf_sensor_count{id=\"" + idLabel + "\"} " + String(sensorCount) + "\n";
  for (uint8_t i = 0; i < sensorCount; i++) {
    String labels = "id=\"" + idLabel + "\",index=\"" + String(i + 1) + "\",name=\"" + prometheusEscaped(String(sensorNames[i])) + "\"";
    if (!isnan(sensorTempsC[i])) {
      m += "uhf_sensor_temp_c{" + labels + "} " + String(sensorTempsC[i], 4) + "\n";
      m += "uhf_sensor_temp_f{" + labels + "} " + String(sensorTempsC[i] * 9.0f / 5.0f + 32.0f, 4) + "\n";
    }
  }
#endif
}

// ── Status / config JSON ───────────────────────────────────────────────────
static void uhfStatusJson(JsonDocument& doc) {
  doc["profilecount"] = profileCount;
  doc["codecount"]    = codeCount;
  doc["txbusy"]       = txBusy;
  doc["txcount"]      = txCount;
  doc["lasttxid"]     = lastTxId;
  doc["lasttxprofileid"] = lastTxProfileId;
  doc["lasttxvalue"]  = lastTxValue;
  doc["lasttxbits"]   = lastTxBits;
  doc["lasttxrepeat"] = lastTxRepeat;
  if (lastTxMs) doc["lasttxagems"] = millis() - lastTxMs;
#ifdef SHARED_LIB_USE_ONEWIRE
  doc["sensorcount"] = sensorCount;
  JsonArray sensors = doc.createNestedArray("sensors");
  for (uint8_t i = 0; i < sensorCount; i++) {
    JsonObject s = sensors.createNestedObject();
    s["index"]=i+1; s["name"]=sensorNames[i]; s["address"]=sensorAddressString(i);
    s["connected"]=sensorPresent[i];
    if (!isnan(sensorTempsC[i])) {
      s["tempc"] = sensorTempsC[i];
      s["tempf"] = sensorTempsC[i] * 9.0f / 5.0f + 32.0f;
    }
  }
#endif
}

static void uhfConfigJson(JsonDocument& doc) {
  // Topics extras
  // (commandtopic etc already present via shared)
}

static bool uhfHelp() {
  consoleLog(CLOG_INFO, "UHF modulator commands:");
  consoleLog(CLOG_INFO, "  status          \xe2\x80\x94 publish a status snapshot");
  consoleLog(CLOG_INFO, "  ls              \xe2\x80\x94 summary of stored profiles + codes");
  consoleLog(CLOG_INFO, "  listprofiles    \xe2\x80\x94 list timing profiles");
  consoleLog(CLOG_INFO, "  listcodes       \xe2\x80\x94 list codes");
  consoleLog(CLOG_INFO, "Or paste raw JSON, e.g. {\"command\":\"tx\",\"id\":\"giandel_on\"}");
#ifdef SHARED_LIB_USE_ONEWIRE
  consoleLog(CLOG_INFO, "  scan            \xe2\x80\x94 rescan 1-Wire bus + read temps");
#endif
  return true;
}

// ── Custom routes ──────────────────────────────────────────────────────────
static void handleApiProfilesGet() {
  StaticJsonDocument<3072> doc;
  appendProfilesToJson(doc);
  webSendJsonDoc(doc);
}

static void handleApiCodesGet() {
  StaticJsonDocument<4096> doc;
  appendCodesToJson(doc);
  webSendJsonDoc(doc);
}

static void handleApiTxPost() {
  if (!webServer.hasArg("id")) { webSendError("missing id", 400); return; }
  String id = webServer.arg("id");
  int idx = findCodeIndexById(id.c_str());
  if (idx < 0) { webSendError("code not found", 404); return; }
  // Defer to main loop tick — TX is blocking and should not run inside web handler
  StaticJsonDocument<256> q;
  q["command"] = "tx";
  q["id"] = id;
  String s; serializeJson(q, s);
  webConsoleCommandPending = s;
  setStatusMessage(("tx " + id).c_str(), 1200);
  webSendOk("tx queued");
}

#ifdef SHARED_LIB_USE_ONEWIRE
static void handleApiScanPost() {
  webRequestSensorScan = true;
  setStatusMessage("scan queued", 1200);
  webSendOk("scan queued");
}
#endif

static void uhfRoutes() {
  webServer.on("/api/profiles", HTTP_GET, handleApiProfilesGet);
  webServer.on("/api/codes",    HTTP_GET, handleApiCodesGet);
  webServer.on("/api/tx",       HTTP_POST, handleApiTxPost);
#ifdef SHARED_LIB_USE_ONEWIRE
  webServer.on("/api/sensors/scan", HTTP_POST, handleApiScanPost);
#endif
}

static void uhfDeferred() {
  serviceUhfDeferred();
}

void registerUhfUiHooks() {
  setMetricsNamePrefix("uhf");
  setMetricsExtra(uhfMetricsExtra);
  setDisplayBodyRenderer(uhfBody);
  setWebStatusJsonFn(uhfStatusJson);
  setWebConfigJsonFn(uhfConfigJson);
  setWebConsoleHelpFn(uhfHelp);
  setWebExtraRoutesFn(uhfRoutes);
  setWebDeferredFn(uhfDeferred);
}
