// web_ui.h - shared web UI plumbing
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Sketch-supplied hooks. All optional.
typedef void (*WebStatusJsonFn)(JsonDocument& doc);     // append fields to /api/status
typedef void (*WebConfigJsonFn)(JsonDocument& doc);     // append fields to /api/config
typedef bool (*WebConsoleHelpFn)(void);                 // print sketch /help lines via consoleLog; return true if handled
typedef void (*WebExtraRoutesFn)(void);                 // register additional routes on `webServer`
typedef void (*WebDeferredFn)(void);                    // run sketch-side deferred work each loop

void setWebStatusJsonFn(WebStatusJsonFn fn);
void setWebConfigJsonFn(WebConfigJsonFn fn);
void setWebConsoleHelpFn(WebConsoleHelpFn fn);
void setWebExtraRoutesFn(WebExtraRoutesFn fn);
void setWebDeferredFn(WebDeferredFn fn);

void startMainWebUi();
void serviceMainWebUi();
void serviceDeferredWebActions();

// Helpers exposed for sketch endpoints
void webSendJsonDoc(JsonDocument& doc, int code = 200);
void webSendOk(const char* msg);
void webSendError(const char* msg, int code = 400);
