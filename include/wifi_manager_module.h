#ifndef WIFI_MANAGER_MODULE_H
#define WIFI_MANAGER_MODULE_H

#include <Arduino.h>

// Function to set up and connect to WiFi
void setupAndConnectWiFi();

// Periodic service routine to be called from loop() to handle reconnection/backoff.
void serviceWifiManager();

// Helpers for diagnostics
bool isWifiConnected();
unsigned long getLastWifiDisconnectMillis();
uint32_t getLastWifiDisconnectReason();
unsigned long getLastWifiReconnectAttemptMillis();
unsigned long getNextWifiReconnectAttemptMillis();
unsigned long getCurrentWifiReconnectBackoffMs();
unsigned long getLastWifiGotIpMillis();
const char* getWifiDisconnectReasonString(uint32_t reason);

#endif // WIFI_MANAGER_MODULE_H
