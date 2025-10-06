#ifndef WEB_API_COMMON_H
#define WEB_API_COMMON_H

#include <ESPAsyncWebServer.h>
#include <Arduino.h>

// Shared server pointer and port (defined elsewhere)
extern AsyncWebServer *server;
extern int webServerPort;

// SD availability flag
extern bool sdReady;

// OTA upload status flags
extern bool otaLastAuthRejected;
extern bool otaLastHadError;
extern bool otaLastSucceeded;
extern String otaLastError;

// Paths and defaults
extern const char* TAG_METADATA_PATH;

// Utility helpers
void setCorsHeaders(AsyncWebServerResponse *response);
void sendCorsJson(AsyncWebServerRequest *request, int code, const char* contentType, const String &payload);

// File streaming helpers
bool handleStreamSdFile(AsyncWebServerRequest *request, const String &path, const char* contentTypeOverride = nullptr);
bool streamSdFileWithGzip(AsyncWebServerRequest *request, const String &path, const char* contentTypeOverride = nullptr);

// Tag metadata helpers
String loadTagMetadataJson();
bool saveTagMetadataJson(const String &payload);

// Modbus config file helpers
String loadModbusConfigJsonFromFile();
bool saveModbusConfigJsonToFile(const String &payload);

// Content type helper
const char* contentTypeFromPath(const String &path);

#endif // WEB_API_COMMON_H
