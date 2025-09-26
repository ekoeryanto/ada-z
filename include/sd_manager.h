#pragma once

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

// SD manager interface
bool sdManagerBegin(uint8_t csPin = 5, uint32_t spiFreq = 1000000);

// Must be called regularly from loop to handle timed actions (1s log, 5min upload)
void sdManagerLoop();

// Helpers used by user code
float readSensor(int sensorPin);
bool logToSD(const String &csvLine);

// Try upload a batch of recent rows (last N minutes) to cloud; returns true on success
bool uploadBatchToCloud();

// Utility: get SD log path
const char* sdLogPath();

// Configure upload endpoint (HTTP POST)
void sdManagerSetUploadUrl(const char* url);

// Set device id and api token (stored in NVS externally) — they will be added to headers
void sdManagerSetDeviceInfo(const String &deviceId, const String &apiToken);

// Optional: force immediate upload
void sdManagerForceUploadNow();
