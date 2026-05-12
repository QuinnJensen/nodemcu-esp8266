// ota_update.cpp - shared Over-The-Air update logic
#include "ota_update.h"
#include <ArduinoOTA.h>
#include <ESP8266HTTPUpdateServer.h>
#include "app_state.h"
#include "display_ui.h"
#include "util.h"

static ESP8266HTTPUpdateServer httpUpdater;
static OtaNotifyFn sStartCb = nullptr;

void setOtaStartCallback(OtaNotifyFn fn) { sStartCb = fn; }

static void onOtaStart() {
  // Notify the sketch to stop sensitive hardware (SSR, radio, etc)
  if (sStartCb) sStartCb();
  
  String type = (ArduinoOTA.getCommand() == U_FLASH) ? "Sketch" : "Filesystem";
  Serial.print("[OTA] Start updating ");
  Serial.println(type);
  showOtaProgress(type.c_str(), 0, 100);
}

static void onOtaProgress(unsigned int progress, unsigned int total) {
  showOtaProgress(nullptr, progress, total);
}

static void onOtaEnd() {
  Serial.println("\n[OTA] End");
  showOtaProgress("Complete! Rebooting...", 100, 100);
}

static void onOtaError(ota_error_t error) {
  Serial.printf("[OTA] Error[%u]: ", error);
  const char* msg = "Error";
  if (error == OTA_AUTH_ERROR) msg = "Auth Failed";
  else if (error == OTA_BEGIN_ERROR) msg = "Begin Failed";
  else if (error == OTA_CONNECT_ERROR) msg = "Connect Failed";
  else if (error == OTA_RECEIVE_ERROR) msg = "Receive Failed";
  else if (error == OTA_END_ERROR) msg = "End Failed";
  Serial.println(msg);
  showOtaProgress(msg, 0, 100);
}

void initOtaUpdate() {
  // 1. Setup ArduinoOTA (Network Push)
  ArduinoOTA.setHostname(safeDeviceId().c_str());
  ArduinoOTA.onStart(onOtaStart);
  ArduinoOTA.onEnd(onOtaEnd);
  ArduinoOTA.onProgress(onOtaProgress);
  ArduinoOTA.onError(onOtaError);
  ArduinoOTA.begin();

  // 2. Setup Web Update Server (Web Upload)
  // Registers /update path on the shared webServer
  httpUpdater.setup(&webServer);
  
  Serial.println("[OTA] Service initialized (/update)");
}

void serviceOtaUpdate() {
  ArduinoOTA.handle();
}
