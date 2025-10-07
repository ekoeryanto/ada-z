#ifndef WEB_API_COMMON_H
#define WEB_API_COMMON_H

#include <ESPAsyncWebServer.h>
#include <Arduino.h>
#include <ArduinoJson.h>

// Shared server pointer and port (defined elsewhere)
extern AsyncWebServer *server;
extern int webServerPort;

// Server-Sent Events (SSE) event sources
extern AsyncEventSource *eventSourceDebug;
extern AsyncEventSource *eventSourceDebugAlias;
extern AsyncEventSource *eventSourceSensors;

// SSE helpers
void pushSseDebugMessage(const char *event, const String &payload);
void pushSensorsSnapshotEvent();
void flagSensorsSnapshotUpdate();
void serviceSensorsSnapshotUpdates();
void ensureSensorSseRegistered(AsyncWebServer *server);

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
// Stream a JsonDocument directly to the client using AsyncResponseStream (no intermediate String)
void sendCorsJsonDoc(AsyncWebServerRequest *request, int code, JsonDocument &doc);

// Convenience wrappers for common {status,message} responses
void sendJsonError(AsyncWebServerRequest *request, int code, const String &message,
                   size_t capacity = 160);
void sendJsonSuccess(AsyncWebServerRequest *request, int code, const String &message = String(""),
                     size_t capacity = 160);

// Tag/index and calibration sampling helpers (moved from web_api.cpp)
int tagToIndex(const String &tag);
void captureCalibrationSamples(int pinIndex, int requestedSamples,
							  float &avgRaw, float &avgSmoothed, float &avgVolt,
							  int &samplesUsed, bool &usedCache);

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
