// metrics_server.h
#pragma once
#include <Arduino.h>

void startMetricsServer();
void serviceMetricsServer();
String buildPrometheusMetrics();
