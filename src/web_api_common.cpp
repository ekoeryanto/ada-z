#include "web_api_common.h"
#include <FS.h>
#include <SD.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <pgmspace.h>

// Definitions of externs declared in header
AsyncWebServer *server = nullptr;
int webServerPort = 0;
bool sdReady = false;

bool otaLastAuthRejected = false;
bool otaLastHadError = false;
bool otaLastSucceeded = false;
String otaLastError = String("");

const char* TAG_METADATA_PATH = "/tags.json";

static const char DEFAULT_TAG_METADATA[] PROGMEM = R"JSON({ "version":1 })JSON";

void setCorsHeaders(AsyncWebServerResponse *response) {
    if (!response) return;
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type, Accept, Origin, Authorization, X-Api-Key");
}

void sendCorsJson(AsyncWebServerRequest *request, int code, const char* contentType, const String &payload) {
    AsyncResponseStream *response = request->beginResponseStream(contentType);
    response->setCode(code);
    setCorsHeaders(response);
    response->print(payload);
    request->send(response);
}

const char* contentTypeFromPath(const String &path) {
    if (path.endsWith(".html")) return "text/html";
    if (path.endsWith(".htm")) return "text/html";
    if (path.endsWith(".js")) return "application/javascript";
    if (path.endsWith(".css")) return "text/css";
    if (path.endsWith(".json")) return "application/json";
    if (path.endsWith(".yaml") || path.endsWith(".yml")) return "application/x-yaml";
    if (path.endsWith(".svg")) return "image/svg+xml";
    if (path.endsWith(".png")) return "image/png";
    if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
    if (path.endsWith(".gif")) return "image/gif";
    if (path.endsWith(".ico")) return "image/x-icon";
    if (path.endsWith(".txt")) return "text/plain";
    return "application/octet-stream";
}

bool handleStreamSdFile(AsyncWebServerRequest *request, const String &path, const char* contentTypeOverride) {
    if (!sdReady) return false;
    if (!SD.exists(path.c_str())) return false;
    const char* ct = contentTypeOverride ? contentTypeOverride : contentTypeFromPath(path);
    request->send(SD, path, ct);
    return true;
}

void setCorsHeaders(AsyncWebServerResponse *response);

bool streamSdFileWithGzip(AsyncWebServerRequest *request, const String &path, const char* contentTypeOverride) {
    if (!sdReady) return false;
    const AsyncWebHeader* aeHeader = request->getHeader("Accept-Encoding");
    String ae = aeHeader ? aeHeader->value() : "";
    bool clientAcceptsGzip = ae.indexOf("gzip") >= 0;
    String gzPath = path + String(".gz");
    if (clientAcceptsGzip && SD.exists(gzPath.c_str())) {
        const char* ct = contentTypeOverride ? contentTypeOverride : contentTypeFromPath(path);
        AsyncWebServerResponse *response = request->beginResponse(SD, gzPath, ct);
        response->addHeader("Content-Encoding", "gzip");
        setCorsHeaders(response);
        request->send(response);
        return true;
    }
    return handleStreamSdFile(request, path, contentTypeOverride);
}

String loadTagMetadataJson() {
    if (sdReady && SD.exists(TAG_METADATA_PATH)) {
        File f = SD.open(TAG_METADATA_PATH, FILE_READ);
        if (f) {
            String payload;
            payload.reserve(f.size());
            while (f.available()) {
                payload += static_cast<char>(f.read());
                delay(0);
            }
            f.close();
            if (payload.length() > 0) return payload;
        }
    }
    return String(FPSTR(DEFAULT_TAG_METADATA));
}

bool saveTagMetadataJson(const String &payload) {
    if (!sdReady) return false;
    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, payload);
    if (err) return false;
    if (!doc["groups"].is<JsonArray>()) return false;
    if (SD.exists(TAG_METADATA_PATH)) SD.remove(TAG_METADATA_PATH);
    File f = SD.open(TAG_METADATA_PATH, FILE_WRITE);
    if (!f) return false;
    size_t written = f.print(payload);
    f.close();
    return written == payload.length();
}

static const char* MODBUS_CONFIG_PATH = "/modbus.json";

String loadModbusConfigJsonFromFile() {
    if (sdReady && SD.exists(MODBUS_CONFIG_PATH)) {
        File f = SD.open(MODBUS_CONFIG_PATH, FILE_READ);
        if (f) {
            String payload;
            payload.reserve(f.size());
            while (f.available()) {
                payload += static_cast<char>(f.read());
                delay(0);
            }
            f.close();
            if (payload.length() > 0) return payload;
        }
    }
    return String();
}

bool saveModbusConfigJsonToFile(const String &payload) {
    if (!sdReady) return false;
    if (SD.exists(MODBUS_CONFIG_PATH)) SD.remove(MODBUS_CONFIG_PATH);
    File f = SD.open(MODBUS_CONFIG_PATH, FILE_WRITE);
    if (!f) return false;
    size_t written = f.print(payload);
    f.close();
    return written == payload.length();
}
