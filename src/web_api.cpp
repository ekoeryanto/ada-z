#include "web_api.h"
#include "config.h" // For HTTP_NOTIFICATION_URL, etc.
#include "pins_config.h" // For AI1_PIN
#include "voltage_pressure_sensor.h" // For calibration
#include "calibration_keys.h" // For calibration key constants
#include "sensor_calibration_types.h" // For SensorCalibration struct
#include "time_sync.h"
#include "sensors_config.h"
#include "http_notifier.h"
#include "sd_logger.h"
#include "current_pressure_sensor.h"
#include "device_id.h"
#include "sample_store.h"
#include "wifi_manager_module.h"
#include <ArduinoJson.h>
#include "storage_helpers.h"
#include "json_helper.h"
#include <WiFi.h>
#include <Update.h>
#include <FS.h>
#include <SPI.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <ESPmDNS.h>
#include <map>

// ArduinoJson usage updated to recommended APIs

// WebServer instance created at runtime so we can choose port depending on WiFi mode
WebServer *server = nullptr;
// Actual port WebServer is bound to (80 for STA, 8080 for AP/portal)
int webServerPort = 0;

// SD availability flag: initialize once at startup to avoid repeated SD.begin() calls
static bool sdReady = false;

static const char* contentTypeFromPath(const String &path) {
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

static bool streamSdFile(const String &path, const char* contentTypeOverride = nullptr) {
    if (!sdReady) return false;
    if (!SD.exists(path.c_str())) return false;
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) return false;
    const char* ct = contentTypeOverride ? contentTypeOverride : contentTypeFromPath(path);
    server->streamFile(f, ct);
    f.close();
    return true;
}

// Serve a file from SD with optional gzip fallback. If the client accepts gzip
// and a .gz sibling exists, serve the gzipped file with Content-Encoding header.
// forward-declare setCorsHeaders so helper can call it even if defined later
void setCorsHeaders();

static bool streamSdFileWithGzip(const String &path, const char* contentTypeOverride = nullptr) {
    if (!sdReady) return false;
    // If client accepts gzip, prefer .gz file
    String ae = server->header("Accept-Encoding");
    bool clientAcceptsGzip = ae.indexOf("gzip") >= 0;

    String gzPath = path + String(".gz");
    if (clientAcceptsGzip && SD.exists(gzPath.c_str())) {
        File f = SD.open(gzPath.c_str(), FILE_READ);
        if (!f) return false;
        const char* ct = contentTypeOverride ? contentTypeOverride : contentTypeFromPath(path);
        setCorsHeaders();
        server->sendHeader("Content-Encoding", "gzip", true);
        server->streamFile(f, ct);
        f.close();
        return true;
    }

    // Fallback to plain file
    return streamSdFile(path, contentTypeOverride);
}

// Track OTA upload outcome between the streaming handler and final HTTP_POST response
static bool otaLastAuthRejected = false;
static bool otaLastHadError = false;
static bool otaLastSucceeded = false;

// Helper: set common CORS headers for responses
void setCorsHeaders() {
    if (!server) return;
    // Ensure CORS preflight allows PUT as well as POST and common headers
    server->sendHeader("Access-Control-Allow-Origin", "*", true);
    server->sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, OPTIONS");
    server->sendHeader("Access-Control-Allow-Headers", "Content-Type, Accept, Origin");
}

// Wrapper to send JSON with CORS headers
void sendCorsJson(int code, const char* contentType, const String &payload) {
    setCorsHeaders();
    if (!server) return;
    server->send(code, contentType, payload);
}

// --- Configuration Keys ---
const char* PREF_NAMESPACE = "config";
const char* PREF_HTTP_URL = "http_url";
// Add other config keys as needed

void saveConfig() {
    // Example: save HTTP_NOTIFICATION_URL if needed
    // saveStringToNVSns(PREF_NAMESPACE, PREF_HTTP_URL, String(HTTP_NOTIFICATION_URL));
    #if ENABLE_VERBOSE_LOGS
    Serial.println("Configuration saved.");
    #endif
}

void loadConfig() {
    // Read persisted values from NVS and print for debug
    String httpUrl = loadStringFromNVSns(PREF_NAMESPACE, PREF_HTTP_URL, String("N/A"));
    #if ENABLE_VERBOSE_LOGS
    Serial.print("Loaded HTTP URL: ");
    Serial.println(httpUrl);
    Serial.println("Configuration loaded.");
    #endif
}

void handleConfig() {
    if (server->method() == HTTP_POST) {
        if (!server->hasArg("plain")) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Missing JSON body\"}");
            return;
        }

        String body = server->arg("plain");
        #if ENABLE_VERBOSE_LOGS
        Serial.print("Received config POST: ");
        Serial.println(body);
        #endif

                JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);

        if (error) {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.f_str());
            sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
            return;
        }

        // Example: Update HTTP_NOTIFICATION_URL (requires a non-macro variable)
        // Persist api_key into NVS so the /update OTA endpoint can validate uploads.
        if (doc["http_notification_url"].is<String>()) {
            #if ENABLE_VERBOSE_LOGS
            Serial.print("New HTTP Notification URL: ");
            Serial.println(doc["http_notification_url"].as<String>());
            #endif
            // saveConfig(); // Call save config after updating variables
        }
        if (doc["api_key"].is<String>()) {
            String newKey = doc["api_key"].as<String>();
            saveStringToNVSns(PREF_NAMESPACE, "api_key", newKey);
            Serial.println("API key saved to NVS (config/api_key)");
        }

        sendCorsJson(200, "application/json", "{\"status\":\"success\", \"message\":\"Config received\"}");
    } else {
        // Handle GET request for current config
            JsonDocument doc;
        // doc["http_notification_url"] = HTTP_NOTIFICATION_URL; // Example: Send current URL
        // Add other config parameters to send

        String response;
        serializeJson(doc, response);
        sendCorsJson(200, "application/json", response);
    }
}

void loadCalibration() {
    // Load calibration values from NVS (if present) for debugging
    float zeroRawAdc = loadFloatFromNVSns(CAL_NAMESPACE, CAL_ZERO_RAW_ADC, 0.0f);
    float spanRawAdc = loadFloatFromNVSns(CAL_NAMESPACE, CAL_SPAN_RAW_ADC, 0.0f);
    float zeroPressureValue = loadFloatFromNVSns(CAL_NAMESPACE, CAL_ZERO_PRESSURE_VALUE, 0.0f);
    float spanPressureValue = loadFloatFromNVSns(CAL_NAMESPACE, CAL_SPAN_PRESSURE_VALUE, 0.0f);

    #if ENABLE_VERBOSE_LOGS
    Serial.println("Calibration loaded:");
    Serial.printf("  Zero Raw ADC: %.2f\n", zeroRawAdc);
    Serial.printf("  Span Raw ADC: %.2f\n", spanRawAdc);
    Serial.printf("  Zero Pressure Value: %.2f\n", zeroPressureValue);
    Serial.printf("  Span Pressure Value: %.2f\n", spanPressureValue);
    #endif
    // In a real scenario, you'd pass these to the pressure sensor module
    // For now, we'll just print it.
}

// Helper: resolve a tag like "AI1" (case-insensitive) to a voltage sensor index (0-based).
// Returns -1 if not recognized. This lets API clients provide tags instead of pin numbers.
static int tagToIndex(const String &tag) {
    if (tag.length() < 3) return -1;
    String s = tag;
    s.toUpperCase();
    if (!s.startsWith("AI")) return -1;
    String num = s.substring(2);
    int n = num.toInt();
    if (n <= 0) return -1;
    int idx = n - 1;
    if (idx >= 0 && idx < getNumVoltageSensors()) return idx;
    return -1;
}

static void captureCalibrationSamples(int pinIndex, int requestedSamples,
                                      float &avgRaw, float &avgSmoothed, float &avgVolt,
                                      int &samplesUsed, bool &usedCache) {
    avgRaw = 0.0f;
    avgSmoothed = 0.0f;
    avgVolt = 0.0f;
    samplesUsed = 0;
    usedCache = false;

    if (requestedSamples < 0) requestedSamples = 0;
    bool haveAvg = false;
    if (requestedSamples > 0) {
        haveAvg = getRecentAverage(pinIndex, requestedSamples, avgRaw, avgSmoothed, avgVolt, samplesUsed);
    } else {
        haveAvg = getRecentAverage(pinIndex, 0, avgRaw, avgSmoothed, avgVolt, samplesUsed);
    }

    if (haveAvg) {
        usedCache = true;
        if (samplesUsed <= 0) {
            samplesUsed = requestedSamples > 0 ? requestedSamples : getSampleCount(pinIndex);
            if (samplesUsed <= 0) samplesUsed = 1;
        }
        return;
    }

    // Fallback to immediate readings
    int pin = getVoltageSensorPin(pinIndex);
    avgRaw = (float)analogRead(pin);
    avgSmoothed = getSmoothedADC(pinIndex);
    if (avgSmoothed <= 0.0f) avgSmoothed = avgRaw;
    avgVolt = getSmoothedVoltagePressure(pinIndex);
    samplesUsed = 1;
    usedCache = false;
}

void handleCalibrate() {
    if (server->method() == HTTP_POST) {
        if (!server->hasArg("plain")) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Missing JSON body\"}");
            return;
        }

        String body = server->arg("plain");
        #if ENABLE_VERBOSE_LOGS
        Serial.print("Received calibrate POST: ");
        Serial.println(body);
        #endif

                JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);

        if (error) {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.f_str());
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Invalid JSON\"}");
            return;
        }

        // Determine which sensor pin/index this calibration applies to. Accept pin_index, pin or sensor_id (AI1)
        int pinIndex = -1;
        if (doc["pin_index"].is<int>()) {
            pinIndex = doc["pin_index"].as<int>();
        } else if (doc["pin"].is<int>()) {
            int pinNumber = doc["pin"].as<int>();
            pinIndex = findVoltageSensorIndexByPin(pinNumber);
        } else if (doc["tag"].is<String>()) {
            String sid = doc["tag"].as<String>();
            pinIndex = tagToIndex(sid);
        }

        if (pinIndex < 0) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Invalid or missing pin_index/pin\"}");
            return;
        }

        // Full explicit calibration values provided
        if (doc["zero_raw_adc"].is<float>() && doc["span_raw_adc"].is<float>() &&
            doc["zero_pressure_value"].is<float>() && doc["span_pressure_value"].is<float>()) {

            float zeroRawAdc = doc["zero_raw_adc"].as<float>();
            float spanRawAdc = doc["span_raw_adc"].as<float>();
            float zeroPressureValue = doc["zero_pressure_value"].as<float>();
            float spanPressureValue = doc["span_pressure_value"].as<float>();

            saveCalibrationForPin(pinIndex, zeroRawAdc, spanRawAdc, zeroPressureValue, spanPressureValue);
            #if ENABLE_VERBOSE_LOGS
            Serial.printf("Calibration saved for pin index %d\n", pinIndex);
            #endif
            sendCorsJson(200, "application/json", "{\"status\":\"success\", \"message\":\"Calibration points saved\"}");
            return;
        }

        // Trigger zero calibration: record current reading as zero
        if (doc["trigger_zero_calibration"].is<bool>() && doc["trigger_zero_calibration"].as<bool>()) {
            float currentRawAdc = getSmoothedADC(pinIndex);
            struct SensorCalibration cal = getCalibrationForPin(pinIndex);
            saveCalibrationForPin(pinIndex, currentRawAdc, cal.spanRawAdc, 0.0f, cal.spanPressureValue);
            Serial.printf("Zero calibration set for index %d: raw=%.2f\n", pinIndex, currentRawAdc);
            sendCorsJson(200, "application/json", "{\"status\":\"success\", \"message\":\"Zero calibration set\"}");
            return;
        }

        // Trigger span calibration: record current reading as span and accept provided span pressure
        if (doc["trigger_span_calibration"].is<bool>() && doc["trigger_span_calibration"].as<bool>() && doc["span_pressure_value"].is<float>()) {
            float currentRawAdc = getSmoothedADC(pinIndex);
            float spanPressureValue = doc["span_pressure_value"].as<float>();
            struct SensorCalibration cal = getCalibrationForPin(pinIndex);
            saveCalibrationForPin(pinIndex, cal.zeroRawAdc, currentRawAdc, cal.zeroPressureValue, spanPressureValue);
            Serial.printf("Span calibration set for index %d: raw=%.2f pressure=%.2f\n", pinIndex, currentRawAdc, spanPressureValue);
            sendCorsJson(200, "application/json", "{\"status\":\"success\", \"message\":\"Span calibration set\"}");
            return;
        }

        sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Invalid calibration parameters\"}");

    } else { // HTTP GET: return calibration for requested pin
        int pinIndex = -1;
        if (server->hasArg("pin_index")) {
            pinIndex = server->arg("pin_index").toInt();
        } else if (server->hasArg("pin")) {
            int pinNumber = server->arg("pin").toInt();
            pinIndex = findVoltageSensorIndexByPin(pinNumber);
        } else if (server->hasArg("tag")) {
            pinIndex = tagToIndex(server->arg("tag"));
        } else {
            // Default to first sensor if none provided
            pinIndex = 0;
        }

        if (pinIndex < 0) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Invalid pin index/number\"}");
            return;
        }

        struct SensorCalibration cal = getCalibrationForPin(pinIndex);
            JsonDocument doc;
        doc["pin_index"] = pinIndex;
        doc["pin"] = getVoltageSensorPin(pinIndex);
    doc["tag"] = String("AI") + String(pinIndex + 1);
        doc["zero_raw_adc"] = cal.zeroRawAdc;
        doc["span_raw_adc"] = cal.spanRawAdc;
        doc["zero_pressure_value"] = cal.zeroPressureValue;
        doc["span_pressure_value"] = cal.spanPressureValue;
        doc["scale"] = cal.scale;
        doc["offset"] = cal.offset;

        String response;
        serializeJson(doc, response);
        sendCorsJson(200, "application/json", response);
    }
}
// Forward-declare the implementation that accepts a port so the
// no-arg wrapper can call it before the implementation appears.
void setupWebServer(int port /*= 80*/);

// No-arg wrapper used by main.cpp (header expects this signature)
void setupWebServer() {
    setupWebServer(80);
}

// Implementation accepting a port
void setupWebServer(int port /*= 80*/) {
    if (server) { delete server; server = nullptr; }
    server = new WebServer(port);

    // Initialize SD once for serving static docs (if present)
    sdReady = false;
    if (SD.begin()) {
        sdReady = true;
        #if ENABLE_VERBOSE_LOGS
        Serial.println("SD initialized for static file serving.");
        #endif
    } else {
        Serial.println("SD not available at startup; static assets will fallback to minimal responses");
    }

    // Static web client at root
    server->on("/", HTTP_GET, []() {
        if (streamSdFileWithGzip(String("/docs/client.html"))) return;
        server->send(200, "text/html",
            "<html><body><h1>press-32</h1><p>Upload docs/client.html to the SD card to serve the web client.</p></body></html>");
    });

    server->on("/api/time/sync", HTTP_POST, []() {
        // Trigger immediate NTP sync and request RTC update if RTC present
        syncNtp(isRtcPresent());
        sendCorsJson(200, "application/json", "{\"status\":\"ok\", \"message\":\"NTP sync triggered\"}");
    });
    // Expose a generic config endpoint to GET/POST small config (persisted to NVS)
    server->on("/api/config", HTTP_ANY, []() {
        handleConfig();
    });
    server->on("/api/time/status", HTTP_GET, []() {
    JsonDocument doc;
        doc["rtc_found"] = isRtcPresent() ? 1 : 0;
        doc["rtc_lost_power"] = isRtcLostPower() ? 1 : 0;
        time_t rtcEpoch = isRtcPresent() ? getRtcEpoch() : 0;
        doc["rtc_epoch"] = (unsigned long)rtcEpoch;
        if (isRtcPresent()) {
            doc["rtc_iso"] = formatIsoWithTz(rtcEpoch);
        } else {
            doc["rtc_iso"] = "";
        }
        time_t sysEpoch = time(nullptr);
        doc["system_epoch"] = (unsigned long)sysEpoch;
        doc["system_iso"] = formatIsoWithTz(sysEpoch);
        doc["last_ntp_epoch"] = (unsigned long)getLastNtpSuccessEpoch();
        doc["last_ntp_iso"] = getLastNtpSuccessIso();
        doc["pending_rtc_sync"] = isPendingRtcSync() ? 1 : 0;
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    server->on("/api/system", HTTP_GET, []() {
        JsonDocument doc;
        bool connected = WiFi.status() == WL_CONNECTED;
        doc["connected"] = connected ? 1 : 0;
        doc["ip"] = WiFi.localIP().toString();
        doc["hostname"] = WiFi.getHostname();
        doc["ssid"] = WiFi.SSID();
        doc["rssi"] = connected ? WiFi.RSSI() : 0;
        doc["uptime_ms"] = (unsigned long)millis();
        doc["rtc_found"] = isRtcPresent() ? 1 : 0;
        doc["rtc_enabled"] = getRtcEnabled() ? 1 : 0;
        doc["last_ntp_epoch"] = (unsigned long)getLastNtpSuccessEpoch();
        doc["last_ntp_iso"] = getLastNtpSuccessIso();
        doc["time_iso"] = getIsoTimestamp();
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    server->on("/api/diagnostics/network", HTTP_GET, []() {
    JsonDocument doc;
        bool connected = isWifiConnected();
        doc["connected"] = connected ? 1 : 0;
        doc["status"] = (int)WiFi.status();
        doc["ssid"] = WiFi.SSID();
        doc["rssi"] = connected ? WiFi.RSSI() : 0;
        doc["ip"] = connected ? WiFi.localIP().toString() : String("");
        doc["last_disconnect_ms"] = getLastWifiDisconnectMillis();
        uint32_t reason = getLastWifiDisconnectReason();
        doc["last_disconnect_reason"] = reason;
        doc["last_disconnect_reason_str"] = String(getWifiDisconnectReasonString(reason));
        doc["last_reconnect_attempt_ms"] = getLastWifiReconnectAttemptMillis();
        doc["next_reconnect_attempt_ms"] = getNextWifiReconnectAttemptMillis();
        doc["reconnect_backoff_ms"] = getCurrentWifiReconnectBackoffMs();
        doc["last_got_ip_ms"] = getLastWifiGotIpMillis();
        doc["uptime_ms"] = millis();
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    // Helper function to handle tag-based sensor reading logic
    auto handleTagRead = [](const String& tag) {
        int sampling = 0;
        if (server->hasArg("sampling")) sampling = server->arg("sampling").toInt();
        if (sampling < 0) sampling = 0;

        if (tag.length() == 0) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing tag\"}");
            return;
        }

        int pinIndex = tagToIndex(tag);
        if (pinIndex < 0) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Unknown tag\"}");
            return;
        }

        int pin = getVoltageSensorPin(pinIndex);
        float avgRaw = 0.0f;
        float avgSmoothed = 0.0f;
        float avgVolt = 0.0f;
        int samplesUsed = 0;
        bool haveAvg = false;

        if (sampling > 0) {
            haveAvg = getRecentAverage(pinIndex, sampling, avgRaw, avgSmoothed, avgVolt, samplesUsed);
        } else {
            haveAvg = getRecentAverage(pinIndex, 0, avgRaw, avgSmoothed, avgVolt, samplesUsed);
        }

        if (!haveAvg) {
            // Fallback to instantaneous readings (single conversion, no delay)
            avgRaw = (float)analogRead(pin);
            avgSmoothed = getSmoothedADC(pinIndex);
            if (avgSmoothed <= 0.0f) avgSmoothed = avgRaw;
            avgVolt = getSmoothedVoltagePressure(pinIndex);
            samplesUsed = 1;
        }

        struct SensorCalibration cal = getCalibrationForPin(pinIndex);
        float converted = haveAvg ? avgVolt : (avgSmoothed * cal.scale) + cal.offset;

        JsonDocument outDoc;
        outDoc["tag"] = tag;
        outDoc["pin_index"] = pinIndex;
        outDoc["pin"] = pin;
        outDoc["samples_requested"] = sampling;
        outDoc["samples_used"] = samplesUsed;
        outDoc["measured_raw_avg"] = roundToDecimals(avgRaw, 2);
        outDoc["measured_filtered_avg"] = roundToDecimals(avgSmoothed, 2);
        outDoc["converted"]["value"] = roundToDecimals(converted, 2);
        outDoc["converted"]["unit"] = "bar";

        String out;
        serializeJson(outDoc, out);
        sendCorsJson(200, "application/json", out);
    };

    // Read a single sensor by tag (supports query ?tag=AI1 or path /tag/AI1)
    // Default sampling is 100; can be overridden with ?sampling=N
    server->on("/api/tag", HTTP_GET, [handleTagRead]() {
        String tag;
        if (server->hasArg("tag")) {
            tag = server->arg("tag");
        } else {
            // no tag query; require path style /tag/<TAG>
            String uri = server->uri();
            if (uri.startsWith("/tag/")) tag = uri.substring(5);
        }
        handleTagRead(tag);
    });

    // Provide support for path-style /tag/AI1 by routing through notFound handler
    server->onNotFound([handleTagRead]() {
        String uri = server->uri();
        const String prefix = "/api/tag/";
        if (!uri.startsWith(prefix)) {
            sendCorsJson(404, "application/json", "{\"status\":\"error\",\"message\":\"Not found\"}");
            return;
        }
        String tag = uri.substring(prefix.length());
        handleTagRead(tag);
    });
    // Secure OTA update endpoint (multipart/form-data upload)
    server->on("/api/update", HTTP_POST, []() {
        if (otaLastAuthRejected) {
            sendCorsJson(401, "application/json", "{\"status\":\"error\",\"message\":\"OTA authentication failed\"}");
        } else if (otaLastHadError || !otaLastSucceeded) {
            sendCorsJson(500, "application/json", "{\"status\":\"error\",\"message\":\"OTA update failed\"}");
        } else {
            sendCorsJson(200, "application/json", "{\"status\":\"ok\",\"message\":\"Update received (rebooting)\"}");
            delay(100);
            ESP.restart();
        }
        otaLastAuthRejected = false;
        otaLastHadError = false;
        otaLastSucceeded = false;
    }, []() {
        // upload handler called repeatedly during file upload
        // Check API key header on first chunk
        static bool auth_ok = false;
        static bool update_begun = false;
        HTTPUpload& upload = server->upload();
        if (upload.status == UPLOAD_FILE_START) {
            auth_ok = false;
            update_begun = false;
            otaLastAuthRejected = false;
            otaLastHadError = false;
            otaLastSucceeded = false;
            // read api key from NVS
            String expected = loadStringFromNVSns(PREF_NAMESPACE, "api_key", String(""));
            String authHeader = server->header("Authorization");
            String apiHeader = server->header("X-Api-Key");
            if (authHeader.length() > 0 && authHeader.startsWith("Bearer ")) {
                String tok = authHeader.substring(7);
                if (tok == expected) auth_ok = true;
            }
            if (!auth_ok && apiHeader.length() > 0 && apiHeader == expected) auth_ok = true;

            if (!auth_ok) {
                Serial.println("Update aborted: auth failed");
                otaLastAuthRejected = true;
                return;
            }

            Serial.printf("Update: start, name: %s\n", upload.filename.c_str());
            uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
            if (!Update.begin(maxSketchSpace)) {
                Update.printError(Serial);
                otaLastHadError = true;
            } else {
                update_begun = true;
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (!update_begun) return;
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                Update.printError(Serial);
                otaLastHadError = true;
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            if (!update_begun) return;
            if (Update.end(true)) {
                Serial.printf("Update Success: %u bytes\n", upload.totalSize);
                otaLastSucceeded = true;
            } else {
                Update.printError(Serial);
                otaLastHadError = true;
            }
        } else if (upload.status == UPLOAD_FILE_ABORTED) {
            Update.end();
            Serial.println("Update aborted");
            otaLastHadError = true;
            update_begun = false;
        }
    });
    // RTC read/set endpoints
    server->on("/api/time/rtc", HTTP_GET, []() {
        JsonDocument doc;
        doc["rtc_found"] = isRtcPresent() ? 1 : 0;
        doc["rtc_lost_power"] = isRtcLostPower() ? 1 : 0;
        time_t rtcEpoch = isRtcPresent() ? getRtcEpoch() : 0;
        doc["rtc_epoch"] = (unsigned long)rtcEpoch;
        if (isRtcPresent()) {
            doc["rtc_iso"] = formatIsoWithTz(rtcEpoch);
        } else {
            doc["rtc_iso"] = "";
        }
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    server->on("/api/time/rtc", HTTP_POST, []() {
        if (!server->hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing body\"}"); return; }
        String body = server->arg("plain");
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (err) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}"); return; }

        if (doc["from_system"].is<bool>() && doc["from_system"].as<bool>()) {
            // Seed RTC from system time
            if (!isRtcPresent()) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"RTC not present\"}"); return; }
            time_t sys = time(nullptr);
            struct tm *tm_sys = localtime(&sys);
            if (!tm_sys) { sendCorsJson(500, "application/json", "{\"status\":\"error\",\"message\":\"System time invalid\"}"); return; }
            DateTime dt(tm_sys->tm_year + 1900, tm_sys->tm_mon + 1, tm_sys->tm_mday, tm_sys->tm_hour, tm_sys->tm_min, tm_sys->tm_sec);
            rtc.adjust(dt);
            String resp = String("{\"status\":\"success\",\"message\":\"RTC set from system time\"}");
            sendCorsJson(200, "application/json", resp);
            return;
        }

        if (doc["iso"].is<String>()) {
            String iso = doc["iso"].as<String>();
            // Expect format YYYY-MM-DDTHH:MM:SSZ or similar
            int Y,M,D,h,m,s;
            if (sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%dZ", &Y,&M,&D,&h,&m,&s) == 6) {
                if (!isRtcPresent()) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"RTC not present\"}"); return; }
                DateTime dt(Y,M,D,h,m,s);
                rtc.adjust(dt);
                String resp = String("{\"status\":\"success\",\"message\":\"RTC set from ISO\"}");
                sendCorsJson(200, "application/json", resp);
                return;
            } else {
                sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid ISO format\"}");
                return;
            }
        }

        sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"No valid action provided\"}");
    });
    server->on("/api/calibrate/pin", HTTP_POST, []() {
        if (!server->hasArg("plain")) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Missing JSON body\"}");
            return;
        }
        String body = server->arg("plain");
    JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);
        if (error) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Invalid JSON\"}");
            return;
        }

        if (!doc["pin"].is<int>()) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Missing pin\"}");
            return;
        }
        int pinNumber = doc["pin"].as<int>();
        int pinIndex = findVoltageSensorIndexByPin(pinNumber);
        if (pinIndex < 0) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Unknown pin\"}");
            return;
        }

        // Non-blocking span calibration: leverage sample store / smoothed data instead of
        // taking synchronous delay-based samples inside the request handler.
        if ((!doc["value"].isNull() && (doc["value"].is<float>() || doc["value"].is<int>())) ||
            (!doc["target"].isNull() && (doc["target"].is<float>() || doc["target"].is<int>()))) {

            float targetPressure = 0.0f;
            if (doc["target"].is<float>() || doc["target"].is<int>()) {
                targetPressure = doc["target"].is<float>() ? doc["target"].as<float>() : (float)doc["target"].as<int>();
            } else {
                targetPressure = doc["value"].is<float>() ? doc["value"].as<float>() : (float)doc["value"].as<int>();
            }

            int requestedSamples = doc["samples"].is<int>() ? doc["samples"].as<int>() : 0;

            float avgRaw, avgSmoothed, avgVolt;
            int samplesUsed;
            bool usedCache;
            captureCalibrationSamples(pinIndex, requestedSamples, avgRaw, avgSmoothed, avgVolt, samplesUsed, usedCache);

            struct SensorCalibration cal = getCalibrationForPin(pinIndex);
            // Save calibration: keep existing zero, set span to measured average raw with provided pressure
            saveCalibrationForPin(pinIndex, cal.zeroRawAdc, avgRaw, cal.zeroPressureValue, targetPressure);
            // Reseed smoothed ADC so readings update immediately
            setupVoltagePressureSensor();

            JsonDocument respDoc;
            respDoc["status"] = "success";
            respDoc["message"] = "Span calibration applied";
            respDoc["pin_index"] = pinIndex;
            respDoc["pin"] = getVoltageSensorPin(pinIndex);
            respDoc["measured_raw_avg"] = roundToDecimals(avgRaw, 2);
            respDoc["measured_filtered_avg"] = roundToDecimals(avgSmoothed, 2);
            respDoc["measured_converted_avg"] = roundToDecimals(avgVolt, 2);
            respDoc["samples_requested"] = requestedSamples;
            respDoc["samples_used"] = samplesUsed;
            respDoc["samples_from_cache"] = usedCache ? 1 : 0;
            respDoc["span_pressure_value"] = targetPressure;
            String resp;
            serializeJson(respDoc, resp);
            sendCorsJson(200, "application/json", resp);
            return;
        }

        // Reuse logic from handleCalibrate: full explicit, trigger_zero, trigger_span
        if (doc["zero_raw_adc"].is<float>() && doc["span_raw_adc"].is<float>() && doc["zero_pressure_value"].is<float>() && doc["span_pressure_value"].is<float>()) {
            saveCalibrationForPin(pinIndex, doc["zero_raw_adc"].as<float>(), doc["span_raw_adc"].as<float>(), doc["zero_pressure_value"].as<float>(), doc["span_pressure_value"].as<float>());
            sendCorsJson(200, "application/json", "{\"status\":\"success\", \"message\":\"Calibration saved\"}");
            return;
        }

        if (doc["trigger_zero_calibration"].is<bool>() && doc["trigger_zero_calibration"].as<bool>()) {
            float currentRawAdc = getSmoothedADC(pinIndex);
            struct SensorCalibration cal = getCalibrationForPin(pinIndex);
            saveCalibrationForPin(pinIndex, currentRawAdc, cal.spanRawAdc, 0.0f, cal.spanPressureValue);
            sendCorsJson(200, "application/json", "{\"status\":\"success\", \"message\":\"Zero calibration set\"}");
            return;
        }

        if (doc["trigger_span_calibration"].is<bool>() && doc["trigger_span_calibration"].as<bool>() && doc["span_pressure_value"].is<float>()) {
            float currentRawAdc = getSmoothedADC(pinIndex);
            struct SensorCalibration cal = getCalibrationForPin(pinIndex);
            saveCalibrationForPin(pinIndex, cal.zeroRawAdc, currentRawAdc, cal.zeroPressureValue, doc["span_pressure_value"].as<float>());
            sendCorsJson(200, "application/json", "{\"status\":\"success\", \"message\":\"Span calibration set\"}");
            return;
        }

        sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Invalid calibration parameters\"}");
    });
    // Convenience: apply a default calibration mapping 0..4095 -> 0..10 bar
    server->on("/api/calibrate/default", HTTP_POST, []() {
        int n = getNumVoltageSensors();
        for (int i = 0; i < n; ++i) {
            saveCalibrationForPin(i, 0.0f, 4095.0f, 0.0f, 10.0f);
        }
        // Reseed smoothed ADCs so readings update immediately after calibration
        setupVoltagePressureSensor();
        sendCorsJson(200, "application/json", "{\"status\":\"success\", \"message\":\"Default calibration applied to all sensors\"}");
    });

    // Convenience: apply default calibration for a single pin by pin
    server->on("/api/calibrate/default/pin", HTTP_POST, []() {
    if (!server->hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing JSON body\"}"); return; }
    String body = server->arg("plain");
            JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (err) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}"); return; }
        int pinIndex = -1;
        if (!doc["pin"].isNull()) {
            int pinNumber = doc["pin"].as<int>();
            pinIndex = findVoltageSensorIndexByPin(pinNumber);
        } else if (!doc["tag"].isNull()) {
            pinIndex = tagToIndex(doc["tag"].as<String>());
        } else {
            sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing pin or tag\"}");
            return;
        }
        if (pinIndex < 0) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Unknown sensor/pin\"}"); return; }
        saveCalibrationForPin(pinIndex, 0.0f, 4095.0f, 0.0f, 10.0f);
        // Reseed smoothed ADC for immediate effect
        setupVoltagePressureSensor();
        sendCorsJson(200, "application/json", "{\"status\":\"success\", \"message\":\"Default calibration applied to pin\"}");
    });

    // ADC-prefixed calibration endpoints for consistency
    // Delegate pin-level calibration to existing handler so both paths behave the same
    server->on("/api/adc/calibrate/pin", HTTP_ANY, []() {
        handleCalibrate();
    });

    // Auto-calibration under /adc prefix: apply span using current smoothed ADC readings
    server->on("/api/adc/calibrate/auto", HTTP_POST, []() {
        if (!server->hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing body\"}"); return; }
        String body = server->arg("plain");
    JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (err) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}"); return; }

        // Build list of targets: map sensor index -> targetPressure
        JsonDocument resp;
        JsonArray results = resp["results"].to<JsonArray>();
        std::map<int, float> targets;
        int requestedSamples = doc["samples"].is<int>() ? doc["samples"].as<int>() : 0;

        if (!doc["sensors"].isNull() && doc["sensors"].is<JsonArray>()) {
            for (JsonObject so : doc["sensors"].as<JsonArray>()) {
                int pinIndex = -1;
                int pinNumber = -1;
                if (!so["pin"].isNull()) {
                    pinNumber = so["pin"].as<int>();
                    pinIndex = findVoltageSensorIndexByPin(pinNumber);
                } else if (!so["tag"].isNull()) {
                    pinIndex = tagToIndex(so["tag"].as<String>());
                    if (pinIndex >= 0) pinNumber = getVoltageSensorPin(pinIndex);
                }

                if (pinIndex < 0) {
                    JsonObject r = results.add<JsonObject>();
                    if (!so["pin"].isNull()) r["pin"] = so["pin"].as<int>();
                    if (!so["tag"].isNull()) r["tag"] = so["tag"].as<String>();
                    r["status"] = "error";
                    r["message"] = "unknown sensor";
                    continue;
                }
                if (so["target"].isNull()) {
                    JsonObject r = results.add<JsonObject>();
                    r["pin_index"] = pinIndex;
                    r["pin"] = pinNumber;
                    r["status"] = "error";
                    r["message"] = "missing target";
                    continue;
                }
                float t = so["target"].is<float>() ? so["target"].as<float>() : (float)so["target"].as<int>();
                targets[pinIndex] = t;
            }
        } else if (!doc["target"].isNull() && (doc["target"].is<float>() || doc["target"].is<int>())) {
            float t = doc["target"].is<float>() ? doc["target"].as<float>() : (float)doc["target"].as<int>();
            int n = getNumVoltageSensors();
            for (int i = 0; i < n; ++i) {
                targets[i] = t;
            }
        } else {
            sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"No target provided\"}");
            return;
        }

        bool appliedAny = false;
        for (auto const& kv : targets) {
            int pinIndex = kv.first;
            float targetPressure = kv.second;
            int pin = getVoltageSensorPin(pinIndex);
            if (pin < 0) {
                JsonObject r = results.add<JsonObject>();
                r["pin_index"] = pinIndex;
                r["status"] = "error";
                r["message"] = "unknown sensor";
                continue;
            }
            float avgRaw, avgSmoothed, avgVolt;
            int samplesUsed;
            bool usedCache;
            captureCalibrationSamples(pinIndex, requestedSamples, avgRaw, avgSmoothed, avgVolt, samplesUsed, usedCache);

            struct SensorCalibration cal = getCalibrationForPin(pinIndex);
            saveCalibrationForPin(pinIndex, cal.zeroRawAdc, avgRaw, cal.zeroPressureValue, targetPressure);
            appliedAny = true;

            JsonObject r = results.add<JsonObject>();
            r["pin"] = pin;
            r["pin_index"] = pinIndex;
            r["measured_raw_avg"] = roundToDecimals(avgRaw, 2);
            r["measured_filtered_avg"] = roundToDecimals(avgSmoothed, 2);
            r["measured_converted_avg"] = roundToDecimals(avgVolt, 2);
            r["samples_requested"] = requestedSamples;
            r["samples_used"] = samplesUsed;
            r["samples_from_cache"] = usedCache ? 1 : 0;
            r["span_pressure_value"] = targetPressure;
            r["status"] = "applied";
        }

        if (appliedAny) {
            setupVoltagePressureSensor();
        }

        String out;
        serializeJson(resp, out);
        sendCorsJson(200, "application/json", out);
    });

    // SD error log endpoints
    server->on("/api/sd/error_log", HTTP_GET, []() {
        int lines = -1;
        if (server->hasArg("lines")) lines = server->arg("lines").toInt();
        String content = readErrorLog(lines);
        if (content.length() == 0) {
            sendCorsJson(200, "text/plain", "");
            return;
        }
        sendCorsJson(200, "text/plain", content);
    });

    server->on("/api/sd/error_log/clear", HTTP_POST, []() {
        clearErrorLog();
        sendCorsJson(200, "application/json", "{\"status\":\"success\",\"message\":\"error log cleared\"}");
    });
    server->on("/api/sd/pending_notifications", HTTP_GET, []() {
    JsonDocument doc;
        doc["sd_enabled"] = getSdEnabled() ? 1 : 0;
        doc["sd_card_found"] = sdCardFound ? 1 : 0;
        size_t pendingCount = countPendingNotifications();
        doc["pending_count"] = (uint32_t)pendingCount;
        doc["file_size"] = (uint32_t)pendingNotificationsFileSize();
        bool includeContent = false;
        if (server->hasArg("include")) {
            includeContent = server->arg("include").toInt() != 0;
        }
        int lines = -1;
        if (server->hasArg("lines")) {
            lines = server->arg("lines").toInt();
        }
        if (includeContent && sdCardFound && pendingCount > 0) {
            doc["content"] = readPendingNotifications(lines);
        }
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    server->on("/api/sd/pending_notifications/clear", HTTP_POST, []() {
        bool ok = clearPendingNotifications();
        if (ok) {
            sendCorsJson(200, "application/json", "{\"status\":\"success\",\"message\":\"pending notifications cleared\"}");
        } else {
            sendCorsJson(500, "application/json", "{\"status\":\"error\",\"message\":\"failed to clear pending notifications\"}");
        }
    });
    // ADS auto-calibration: compute and persist tp_scale (mV per mA) so TP5551-derived voltage maps to target pressure
    server->on("/api/ads/calibrate/auto", HTTP_POST, []() {
        if (!server->hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing body\"}"); return; }
        String body = server->arg("plain");
    JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (err) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}"); return; }

        // Build list of targets: map channel -> targetPressure
        std::map<int, float> targets;
        if (!doc["channels"].isNull() && doc["channels"].is<JsonArray>()) {
            for (JsonObject chObj : doc["channels"].as<JsonArray>()) {
                if (!chObj["channel"].isNull() && !chObj["target"].isNull()) {
                    int ch = chObj["channel"].as<int>();
                    float t = (float)chObj["target"].as<float>();
                    targets[ch] = t;
                }
            }
    } else if (!doc["target"].isNull() && (doc["target"].is<float>() || doc["target"].is<int>())) {
            float t = doc["target"].is<float>() ? doc["target"].as<float>() : (float)doc["target"].as<int>();
            // apply to default ADS channels (0..1)
            for (int ch = 0; ch <= 1; ++ch) targets[ch] = t;
        } else {
            sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"No target provided\"}");
            return;
        }

    JsonDocument resp;
    JsonArray results = resp["results"].to<JsonArray>();

        for (auto const& kv : targets) {
            int ch = kv.first;
            float targetPressure = kv.second;
            if (ch < 0 || ch > 3) {
                JsonObject r = results.add<JsonObject>();
                r["channel"] = ch;
                r["status"] = "error";
                r["message"] = "invalid channel";
                continue;
            }

            // Read current smoothed mA for channel
            float ma_smoothed = getAdsSmoothedMa(ch);
            if (ma_smoothed <= 0.0f) {
                JsonObject r = results.add<JsonObject>();
                r["channel"] = ch;
                r["status"] = "error";
                r["message"] = "insufficient measured current (<=0)";
                continue;
            }

            // Compute required TP voltage (mV) for target pressure using same mapping as readings:
            // voltage_v = (targetPressure / DEFAULT_RANGE_BAR) * 10.0
            // mV_needed = voltage_v * 1000
            float voltage_v = (targetPressure / DEFAULT_RANGE_BAR) * 10.0f;
            float mv_needed = voltage_v * 1000.0f;

            // New tp_scale = mV_needed / measured_mA
            float new_tp_scale = mv_needed / ma_smoothed;

            // Persist into calibration namespace as key "tp_scale_%d"
            char key[16]; snprintf(key, sizeof(key), "tp_scale_%d", ch);
            saveFloatToNVSns(CAL_NAMESPACE, key, new_tp_scale);

            JsonObject r = results.add<JsonObject>();
            r["channel"] = ch;
            r["measured_ma"] = ma_smoothed;
            r["applied_tp_scale_mv_per_ma"] = new_tp_scale;
            r["span_pressure_value"] = targetPressure;
            r["status"] = "applied";
        }

        String out;
        serializeJson(resp, out);
        sendCorsJson(200, "application/json", out);
    });
    // Auto-calibration endpoint: apply span calibration using current smoothed readings
    // POST body options:
    //  { "target": 4.8 }                -> apply target to all ADC sensors
    //  { "sensors": [ {"pin":35, "target":4.8}, ... ] } -> per-pin targets
    server->on("/api/calibrate/auto", HTTP_POST, []() {
        if (!server->hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing body\"}"); return; }
        String body = server->arg("plain");
    JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (err) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}"); return; }

        // Build list of targets: map sensor index -> targetPressure
        JsonDocument resp;
        JsonArray results = resp["results"].to<JsonArray>();
        std::map<int, float> targets; // sensorIndex -> pressure
        int requestedSamples = doc["samples"].is<int>() ? doc["samples"].as<int>() : 0;

        if (!doc["sensors"].isNull() && doc["sensors"].is<JsonArray>()) {
            for (JsonObject so : doc["sensors"].as<JsonArray>()) {
                int pinIndex = -1;
                int pinNumber = -1;
                if (!so["pin"].isNull()) {
                    pinNumber = so["pin"].as<int>();
                    pinIndex = findVoltageSensorIndexByPin(pinNumber);
                } else if (!so["tag"].isNull()) {
                    pinIndex = tagToIndex(so["tag"].as<String>());
                    if (pinIndex >= 0) pinNumber = getVoltageSensorPin(pinIndex);
                }

                if (pinIndex < 0) {
                    JsonObject r = results.add<JsonObject>();
                    if (!so["pin"].isNull()) r["pin"] = so["pin"].as<int>();
                    if (!so["tag"].isNull()) r["tag"] = so["tag"].as<String>();
                    r["status"] = "error";
                    r["message"] = "unknown sensor";
                    continue;
                }
                if (so["target"].isNull()) {
                    JsonObject r = results.add<JsonObject>();
                    r["pin_index"] = pinIndex;
                    r["pin"] = pinNumber;
                    r["status"] = "error";
                    r["message"] = "missing target";
                    continue;
                }

                float t = so["target"].is<float>() ? so["target"].as<float>() : (float)so["target"].as<int>();
                targets[pinIndex] = t;
            }
        } else if (!doc["target"].isNull() && (doc["target"].is<float>() || doc["target"].is<int>())) {
            float t = doc["target"].is<float>() ? doc["target"].as<float>() : (float)doc["target"].as<int>();
            // apply to all ADC sensors
            int n = getNumVoltageSensors();
            for (int i = 0; i < n; ++i) {
                targets[i] = t;
            }
        } else {
            sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"No target provided\"}");
            return;
        }

        bool appliedAny = false;
        // For each target, measure current smoothed ADC and save as span
        for (auto const& kv : targets) {
            int pinIndex = kv.first;
            float targetPressure = kv.second;
            int pin = getVoltageSensorPin(pinIndex);
            if (pin < 0) {
                JsonObject r = results.add<JsonObject>();
                r["pin_index"] = pinIndex;
                r["status"] = "error";
                r["message"] = "unknown sensor";
                continue;
            }

            float avgRaw, avgSmoothed, avgVolt;
            int samplesUsed;
            bool usedCache;
            captureCalibrationSamples(pinIndex, requestedSamples, avgRaw, avgSmoothed, avgVolt, samplesUsed, usedCache);

            struct SensorCalibration cal = getCalibrationForPin(pinIndex);
            saveCalibrationForPin(pinIndex, cal.zeroRawAdc, avgRaw, cal.zeroPressureValue, targetPressure);
            appliedAny = true;

            JsonObject r = results.add<JsonObject>();
            r["pin"] = pin;
            r["pin_index"] = pinIndex;
            r["measured_raw_avg"] = roundToDecimals(avgRaw, 2);
            r["measured_filtered_avg"] = roundToDecimals(avgSmoothed, 2);
            r["measured_converted_avg"] = roundToDecimals(avgVolt, 2);
            r["samples_requested"] = requestedSamples;
            r["samples_used"] = samplesUsed;
            r["samples_from_cache"] = usedCache ? 1 : 0;
            r["span_pressure_value"] = targetPressure;
            r["status"] = "applied";
        }

        if (appliedAny) {
            setupVoltagePressureSensor();
        }

        String out;
        serializeJson(resp, out);
        sendCorsJson(200, "application/json", out);
    });
    server->on("/api/calibrate", handleCalibrate);
    server->on("/api/calibrate/all", HTTP_GET, []() {
    JsonDocument doc;
            for (int i = 0; i < getNumVoltageSensors(); ++i) {
                struct SensorCalibration cal = getCalibrationForPin(i);
            JsonObject obj = doc[String(i)].to<JsonObject>();
            obj["pin_index"] = i;
                obj["pin"] = getVoltageSensorPin(i);
                obj["tag"] = String("AI") + String(i + 1);
            obj["zero_raw_adc"] = cal.zeroRawAdc;
            obj["span_raw_adc"] = cal.spanRawAdc;
            obj["zero_pressure_value"] = cal.zeroPressureValue;
            obj["span_pressure_value"] = cal.spanPressureValue;
            obj["scale"] = cal.scale;
            obj["offset"] = cal.offset;
        }
        String response;
        serializeJson(doc, response);
        sendCorsJson(200, "application/json", response);
    });
    // --- Sensors config endpoints ---
    server->on("/api/sensors/config", HTTP_GET, []() {
        int n = getConfiguredNumSensors();
    JsonDocument doc;
        doc["num_sensors"] = n;
    JsonArray arr = doc["sensors"].to<JsonArray>();
        for (int i = 0; i < n; ++i) {
            JsonObject obj = arr.add<JsonObject>();
            obj["sensor_index"] = i;
            obj["sensor_pin"] = getVoltageSensorPin(i);
            obj["enabled"] = getSensorEnabled(i);
            obj["notification_interval_ms"] = getSensorNotificationInterval(i);
        }
        String response;
        serializeJson(doc, response);
        sendCorsJson(200, "application/json", response);
    });

    server->on("/api/sensors/config", HTTP_POST, []() {
        if (!server->hasArg("plain")) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Missing JSON body\"}");
            return;
        }
        String body = server->arg("plain");
    JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (err) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\": \"Invalid JSON\"}");
            return;
        }

        if (!doc["sensors"].is<JsonArray>()) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\": \"Missing sensors array\"}");
            return;
        }

        JsonArray arr = doc["sensors"].as<JsonArray>();
        for (JsonObject sensor : arr) {
            int idx = sensor["sensor_index"].as<int>();
            bool enabled = sensor["enabled"].is<bool>() ? sensor["enabled"].as<bool>() : getSensorEnabled(idx);
            unsigned long interval = sensor["notification_interval_ms"].is<unsigned long>() ? sensor["notification_interval_ms"].as<unsigned long>() : getSensorNotificationInterval(idx);
            // Update runtime
            setSensorEnabled(idx, enabled);
            setSensorNotificationInterval(idx, interval);
            // Persist
            String enKey = String(PREF_SENSOR_ENABLED_PREFIX) + String(idx);
            String ivKey = String(PREF_SENSOR_INTERVAL_PREFIX) + String(idx);
            saveIntToNVSns("sensors", enKey.c_str(), enabled ? 1 : 0);
            saveULongToNVSns("sensors", ivKey.c_str(), interval);
        }
        

        sendCorsJson(200, "application/json", "{\"status\":\"success\", \"message\":\"Sensor config updated\"}");
    });
    // Live sensor readings: raw ADC (current analogRead), smoothed ADC, and calibrated voltage
    server->on("/api/sensors/readings", HTTP_GET, []() {
        int n = getNumVoltageSensors();
        const int ads_channels = 2; // currently reporting ADS1115 ch 0..1
        String fromUnit = "";
        if (server->hasArg("convert_from")) fromUnit = server->arg("convert_from");
    JsonDocument doc;
        // Add normalized ISO timestamp using time_sync helper
        doc["timestamp"] = getIsoTimestamp();
        // Add network / IP details so clients can correlate readings with current network state
        {
            JsonObject net = doc["network"].to<JsonObject>();
            net["connected"] = isWifiConnected() ? 1 : 0;
            net["ip"] = isWifiConnected() ? WiFi.localIP().toString() : String("");
            net["gateway"] = isWifiConnected() ? WiFi.gatewayIP().toString() : String("");
            net["mac"] = WiFi.macAddress();
            net["rssi"] = isWifiConnected() ? WiFi.RSSI() : 0;
        }
        // Expose the RTU at top-level and create a flat `tags` array of sensor objects
        String rtuId = String(getChipId());
        doc["rtu"] = rtuId;
    JsonArray tags = doc["tags"].to<JsonArray>();

        // First, collect ADC sensors grouped by RTU
        for (int i = 0; i < n; ++i) {
            int pin = getVoltageSensorPin(i);
            int raw = analogRead(pin);
            float smoothed = getSmoothedADC(i);
            // Prefer averaged values from sample store for API/read responses
            float avgRawF, avgSmoothedF, avgVoltF;
            if (getAverages(i, avgRawF, avgSmoothedF, avgVoltF)) {
                raw = (int)round(avgRawF);
                smoothed = avgSmoothedF;
                // Note: sample_store stores 'volt' as the calibrated pressure for historical reasons;
                // we will compute pressure_from_smoothed explicitly below to avoid ambiguity.
            }
            int raw_original = raw;
            int raw_effective = raw;
            bool saturated = isPinSaturated(i);
            if (raw == 4095 && !saturated) raw_effective = (int)round(smoothed);
            float voltage_smoothed_v = convert010V(round(smoothed));
            struct SensorCalibration cal = getCalibrationForPin(i);
            float pressure_from_raw = (raw_effective * cal.scale) + cal.offset;
            // Always compute pressure from the smoothed ADC value (filtered) for consistency
            float pressure_from_smoothed = (round(smoothed) * cal.scale) + cal.offset;
            // Use the pressure_from_smoothed as the authoritative converted.value
            float pressure_final = pressure_from_smoothed;
            JsonObject s = tags.add<JsonObject>();
            s["id"] = String("AI") + String(i + 1);
            s["port"] = pin;
            s["index"] = i;
            s["source"] = "adc";
            s["enabled"] = getSensorEnabled(i) ? 1 : 0;

            JsonObject val = s["value"].to<JsonObject>();
            // `raw` is the true raw ADC value (murni)
            val["raw"] = raw;
            val["filtered"] = smoothed;
            // ADC: include scaled forms for raw and filtered (voltages) as well as the primary scaled value
            JsonObject scaled = val["scaled"].to<JsonObject>();
            // scaled.raw := voltage derived from the raw ADC reading
            scaled["raw"] = roundToDecimals(convert010V(raw), 3);
            // scaled.filtered := voltage derived from the smoothed ADC reading
            scaled["filtered"] = roundToDecimals(voltage_smoothed_v, 3);
            // primary displayed scaled value (kept for backwards compatibility)
            scaled["value"] = roundToDecimals(voltage_smoothed_v, 2);
            scaled["unit"] = "volt";
            JsonObject conv = val["converted"].to<JsonObject>();
            conv["value"] = roundToDecimals(pressure_final, 2);
            conv["unit"] = "bar";
            conv["semantic"] = "pressure";
            conv["raw"] = roundToDecimals(pressure_from_raw, 2);
            conv["filtered"] = roundToDecimals(pressure_from_smoothed, 2);

            // Audit fields: compare measured voltage with expected voltage derived from calibrated pressure
            JsonObject audit = val["audit"].to<JsonObject>();
            float measured_voltage_v = voltage_smoothed_v;
            float expected_voltage_v = (pressure_final / DEFAULT_RANGE_BAR) * 10.0f; // expected 0..10V mapping
            audit["measured_voltage_v"] = roundToDecimals(measured_voltage_v, 3);
            audit["expected_voltage_v_from_pressure"] = roundToDecimals(expected_voltage_v, 3);
            audit["voltage_delta_v"] = roundToDecimals(measured_voltage_v - expected_voltage_v, 3);

            JsonObject meta = s["meta"].to<JsonObject>();
            meta["cal_zero_raw_adc"] = cal.zeroRawAdc;
            meta["cal_span_raw_adc"] = cal.spanRawAdc;
            meta["cal_zero_pressure_value"] = cal.zeroPressureValue;
            meta["cal_span_pressure_value"] = cal.spanPressureValue;
            meta["cal_scale"] = cal.scale;
            meta["cal_offset"] = cal.offset;
            meta["saturated"] = saturated ? 1 : 0;
        }

        // ADS channels: include under the RTU as well
        for (int ch = 0; ch <= 1; ++ch) {
            int16_t raw = readAdsRaw(ch);
            float mv = adsRawToMv(raw);
            float tp_scale = getAdsTpScale(ch);
            float ma = readAdsMa(ch, DEFAULT_SHUNT_OHM, DEFAULT_AMP_GAIN);
            float depth_mm = computeDepthMm(ma, DEFAULT_CURRENT_INIT_MA, DEFAULT_RANGE_MM, DEFAULT_DENSITY_WATER);
            // Map scaled voltage directly to pressure assuming sensor outputs 0-10V -> 0-DEFAULT_RANGE_BAR
            float voltage_v = mv / 1000.0f;
            float pressure_bar = (voltage_v / 10.0f) * DEFAULT_RANGE_BAR;

            JsonObject s = tags.add<JsonObject>();
            s["id"] = String("ADS_A") + String(ch);
            s["port"] = ch;
            s["index"] = n + ch;
            s["source"] = "ads1115";

            JsonObject val = s["value"].to<JsonObject>();
            // `raw` is the true raw ADS value
            val["raw"] = raw;
            // Provide a filtered measurement for ADS channels (smoothed voltage derived from smoothed mA)
            float ma_smoothed = getAdsSmoothedMa(ch);
            float mv_from_smoothed = ma_smoothed * tp_scale;
            float voltage_from_smoothed = mv_from_smoothed / 1000.0f;
            val["filtered"] = roundToDecimals(voltage_from_smoothed, 3);
            // ADS: move measurement into meta for ADS channels
            JsonObject scaled = val["scaled"].to<JsonObject>();
            scaled["raw"] = roundToDecimals(mv / 1000.0f, 3);
            scaled["filtered"] = roundToDecimals(voltage_from_smoothed, 3);
            scaled["value"] = roundToDecimals(mv / 1000.0f, 2);
            scaled["unit"] = "volt";
            JsonObject convA = val["converted"].to<JsonObject>();
            convA["value"] = roundToDecimals(pressure_bar, 2);
            convA["unit"] = "bar";
            convA["semantic"] = "pressure";
            convA["note"] = "TP5551 derived";
            // Provide from_raw and from_filtered (smoothed) converted pressure for ADS
            convA["raw"] = roundToDecimals(pressure_bar, 2);
            float pressure_from_smoothed = (voltage_from_smoothed / 10.0f) * DEFAULT_RANGE_BAR;
            convA["filtered"] = roundToDecimals(pressure_from_smoothed, 2);

            // Audit fields for ADS channel: measured mv vs expected mv from converted pressure
            JsonObject audit = val["audit"].to<JsonObject>();
            float measured_voltage_ads_v = mv / 1000.0f;
            float expected_voltage_ads_v = (convA["value"].is<float>() ? convA["value"].as<float>() : (float)convA["value"].as<int>()) / DEFAULT_RANGE_BAR * 10.0f;
            audit["measured_voltage_v"] = roundToDecimals(measured_voltage_ads_v, 3);
            audit["expected_voltage_v_from_pressure"] = roundToDecimals(expected_voltage_ads_v, 3);
            audit["voltage_delta_v"] = roundToDecimals(measured_voltage_ads_v - expected_voltage_ads_v, 3);
            JsonObject meta = s["meta"].to<JsonObject>();
            JsonObject meta_meas = meta["measurement"].to<JsonObject>();
            meta_meas["mv"] = mv;
            meta_meas["ma"] = ma;
            meta["tp_model"] = String("TP5551");
            meta["tp_scale_mv_per_ma"] = tp_scale;
            // Also expose tp_scale as a calibration value under cal_* for consistency
            meta["cal_tp_scale_mv_per_ma"] = tp_scale;
            meta["ma_smoothed"] = ma_smoothed;
            meta["depth_mm"] = depth_mm;
        }

    // tags_total is number of sensor entries returned in `tags`
    doc["tags_total"] = tags.size();

    String resp;
    serializeJson(doc, resp);
    sendCorsJson(200, "application/json", resp);
    });

    // Notification config endpoints
    server->on("/api/notifications/config", HTTP_GET, []() {
    int mode = loadIntFromNVSns(PREF_NAMESPACE, PREF_NOTIFICATION_MODE, DEFAULT_NOTIFICATION_MODE);
    int payload = loadIntFromNVSns(PREF_NAMESPACE, PREF_NOTIFICATION_PAYLOAD, DEFAULT_NOTIFICATION_PAYLOAD_TYPE);    
    JsonDocument doc;
    doc["mode"] = mode;
    doc["payload_type"] = payload;
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    server->on("/api/notifications/config", HTTP_POST, []() {
    if (!server->hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing body\"}"); return; }
    String body = server->arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}"); return; }

        int mode = doc["mode"].is<int>() ? doc["mode"].as<int>() : DEFAULT_NOTIFICATION_MODE;
        int payload = doc["payload_type"].is<int>() ? doc["payload_type"].as<int>() : DEFAULT_NOTIFICATION_PAYLOAD_TYPE;

    saveIntToNVSns(PREF_NAMESPACE, PREF_NOTIFICATION_MODE, mode);
    saveIntToNVSns(PREF_NAMESPACE, PREF_NOTIFICATION_PAYLOAD, payload);

        setNotificationMode((uint8_t)mode);
        setNotificationPayloadType((uint8_t)payload);

        sendCorsJson(200, "application/json", "{\"status\":\"success\",\"message\":\"Notification config updated\"}");
    });
    // ADS channel configuration endpoints: view and set per-channel shunt and amp gain
    server->on("/api/ads/config", HTTP_GET, []() {
    JsonDocument doc;
        JsonArray arr = doc["channels"].to<JsonArray>();
        for (int ch = 0; ch <= 1; ++ch) {
            JsonObject o = arr.add<JsonObject>();
            o["channel"] = ch;
            // Expose TP5551 info and channel mode
            o["tp_model"] = String("TP5551");
            o["tp_scale_mv_per_ma"] = getAdsTpScale(ch);
            o["ads_mode"] = getAdsChannelMode(ch);
        }
        // Expose smoothing params
    float ema = loadFloatFromNVSns("ads_cfg", "ema_alpha", 0.1f);
    int numavg = loadIntFromNVSns("ads_cfg", "num_avg", 5);
    doc["ema_alpha"] = ema;
    doc["num_avg"] = numavg;

        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    // Accept PUT as well as POST for /ads/config so client PUTs are supported
    server->on("/api/ads/config", HTTP_PUT, []() {
        if (!server->hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Missing body\"}"); return; }
        String body = server->arg("plain");
    JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (err) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\": \"Invalid JSON\"}");
            return;
        }
        if (!doc["channels"].is<JsonArray>()) { sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\": \"Missing channels array\"}"); return; }
        // Use storage helpers to persist ads_cfg values
        // Preferences p;
        // p.begin("ads_cfg", false);
        for (JsonObject chObj : doc["channels"].as<JsonArray>()) {
            int ch = chObj["channel"].as<int>();
            if (ch < 0 || ch > 3) continue;
            if (!chObj["shunt_ohm"].isNull()) {
                char key[16]; snprintf(key, sizeof(key), "shunt_%d", ch);
                saveFloatToNVSns("ads_cfg", key, (float)chObj["shunt_ohm"].as<float>());
            }
            if (!chObj["amp_gain"].isNull()) {
                char key[16]; snprintf(key, sizeof(key), "amp_%d", ch);
                saveFloatToNVSns("ads_cfg", key, (float)chObj["amp_gain"].as<float>());
            }
            if (!chObj["ads_mode"].isNull()) {
                char key[16]; snprintf(key, sizeof(key), "mode_%d", ch);
                saveIntToNVSns("ads_cfg", key, (int)chObj["ads_mode"].as<int>());
            }
            if (!chObj["tp_scale_mv_per_ma"].isNull()) {
                char key[16]; snprintf(key, sizeof(key), "tp_scale_%d", ch);
                saveFloatToNVSns(CAL_NAMESPACE, key, (float)chObj["tp_scale_mv_per_ma"].as<float>());
            }
        }
        if (!doc["ema_alpha"].isNull()) {
            float ema = (float)doc["ema_alpha"].as<float>();
            saveFloatToNVSns("ads_cfg", "ema_alpha", ema);
            setAdsEmaAlpha(ema);
        }
        if (!doc["num_avg"].isNull()) {
            int na = (int)doc["num_avg"].as<int>();
            saveIntToNVSns("ads_cfg", "num_avg", na);
            setAdsNumAvg(na);
        }
        sendCorsJson(200, "application/json", "{\"status\":\"success\", \"message\":\"ADS config saved\"}");
    });

    // ADC smoothing/runtime sample-store configuration
    server->on("/api/adc/config", HTTP_GET, []() {
    JsonDocument doc;
        doc["adc_num_samples"] = getAdcNumSamples();
        doc["samples_per_sensor"] = getSampleCapacity();
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    server->on("/api/adc/config", HTTP_POST, []() {
        if (!server->hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing body\"}"); return; }
        String body = server->arg("plain");
    JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (err) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}"); return; }
        bool changed = false;
    if (!doc["adc_num_samples"].isNull()) {
            int ns = doc["adc_num_samples"].as<int>();
            setAdcNumSamples(ns);
            // Persist adc_num_samples so it survives reboot
            saveIntToNVSns("adc_cfg", "num_samples", ns);
            changed = true;
        }
    if (!doc["samples_per_sensor"].isNull()) {
            int sp = doc["samples_per_sensor"].as<int>();
            resizeSampleStore(sp);
            // Persist the chosen capacity so reboots preserve it
            saveIntToNVSns("adc_cfg", "samples_per_sensor", sp);
            changed = true;
        }
    if (!doc["divider_mv"].isNull()) {
            float dv = doc["divider_mv"].is<float>() ? doc["divider_mv"].as<float>() : (float)doc["divider_mv"].as<int>();
            if (dv > 0.0f) {
                saveFloatToNVSns("adc_cfg", "divider_mv", dv);
                changed = true;
            }
        }
        if (changed) sendCorsJson(200, "application/json", "{\"status\":\"success\",\"message\":\"ADC config updated\"}");
        else sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"No supported keys provided\"}");
    });

    // Reseed ADC smoothed values and clear in-memory sample buffers without reboot
    server->on("/api/adc/reseed", HTTP_POST, []() {
        // Clear persisted sample buffers and runtime buffers
        clearSampleStore();
        // Re-initialize smoothed ADCs by reseeding from current ADC readings
        setupVoltagePressureSensor();
        sendCorsJson(200, "application/json", "{\"status\":\"success\", \"message\":\"ADC smoothed values reseeded and sample buffers cleared\"}");
    });

    // Ensure OPTIONS preflight supported for /adc/reseed
    server->on("/api/adc/reseed", HTTP_OPTIONS, []() {
        setCorsHeaders();
        sendCorsJson(204, "text/plain", "");
    });

    // Reseed ADS smoothing buffers (clear median/EMA) after calibration or tp_scale change
    server->on("/api/ads/reseed", HTTP_POST, []() {
        clearAdsBuffers();
        sendCorsJson(200, "application/json", "{\"status\":\"success\", \"message\":\"ADS buffers cleared and reseeded\"}");
    });

    // OPTIONS preflight for /ads/reseed
    server->on("/api/ads/reseed", HTTP_OPTIONS, []() {
        setCorsHeaders();
        sendCorsJson(204, "text/plain", "");
    });

    // Ensure explicit OPTIONS preflight handler for /ads/config
    server->on("/api/ads/config", HTTP_OPTIONS, []() {
        setCorsHeaders();
        sendCorsJson(204, "text/plain", "");
    });

    server->on("/api/ads/config", HTTP_POST, []() {
        if (!server->hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing body\"}"); return; }
        String body = server->arg("plain");
    JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (err) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}"); return; }
        if (!doc["channels"].is<JsonArray>()) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing channels array\"}"); return; }
        // Use storage_helpers to persist ads_cfg values
        for (JsonObject chObj : doc["channels"].as<JsonArray>()) {
            int ch = chObj["channel"].as<int>();
            if (ch < 0 || ch > 3) continue;
            if (!chObj["shunt_ohm"].isNull()) {
                char key[16]; snprintf(key, sizeof(key), "shunt_%d", ch);
                saveFloatToNVSns("ads_cfg", key, (float)chObj["shunt_ohm"].as<float>());
            }
            if (!chObj["amp_gain"].isNull()) {
                char key[16]; snprintf(key, sizeof(key), "amp_%d", ch);
                saveFloatToNVSns("ads_cfg", key, (float)chObj["amp_gain"].as<float>());
            }
            if (!chObj["ads_mode"].isNull()) {
                char key[16]; snprintf(key, sizeof(key), "mode_%d", ch);
                saveIntToNVSns("ads_cfg", key, (int)chObj["ads_mode"].as<int>());
            }
            if (!chObj["tp_scale_mv_per_ma"].isNull()) {
                char key[16]; snprintf(key, sizeof(key), "tp_scale_%d", ch);
                // Persist tp_scale into calibration namespace for unified access
                saveFloatToNVSns(CAL_NAMESPACE, key, (float)chObj["tp_scale_mv_per_ma"].as<float>());
            }
        }
        // Optionally accept smoothing params at top level
    if (!doc["ema_alpha"].isNull()) {
            float ema = (float)doc["ema_alpha"].as<float>();
            saveFloatToNVSns("ads_cfg", "ema_alpha", ema);
            setAdsEmaAlpha(ema);
        }
    if (!doc["num_avg"].isNull()) {
            int na = (int)doc["num_avg"].as<int>();
            saveIntToNVSns("ads_cfg", "num_avg", na);
            setAdsNumAvg(na);
        }
        sendCorsJson(200, "application/json", "{\"status\":\"success\",\"message\":\"ADS config saved\"}");
    });
    // Trigger notification(s) on demand: POST body may include { "sensor_index": int } or { "pin": int } (accepts legacy "pin_number" too)
    server->on("/api/notifications/trigger", HTTP_POST, []() {
    if (!server->hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing body\"}"); return; }
        String body = server->arg("plain");
    JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
    if (err) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}"); return; }

        // Determine target: specific ADC sensor, ADS channel, or all
        int targetIndex = -1;
        bool handled = false;
        if (doc["sensor_index"].is<int>()) {
            targetIndex = doc["sensor_index"].as<int>();
                if (targetIndex < 0 || targetIndex >= getNumVoltageSensors()) {
                sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid sensor_index\"}");
                return;
            }
        } else if (doc["pin"].is<int>() || doc["pin_number"].is<int>()) {
            int pin = doc["pin"].is<int>() ? doc["pin"].as<int>() : doc["pin_number"].as<int>();
            targetIndex = findVoltageSensorIndexByPin(pin);
            if (targetIndex < 0) {
                sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Unknown pin\"}");
                return;
            }
        } else if (doc["ads_channel"].is<int>()) {
            int ch = doc["ads_channel"].as<int>();
            // validate channel number (assume 0..1 currently)
            if (ch < 0 || ch > 3) {
                sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid ads_channel\"}");
                return;
            }
            // Read ADS values and send single ADS notification
            int16_t raw = readAdsRaw(ch);
            float mv = adsRawToMv(raw);
            float ma = readAdsMa(ch, DEFAULT_SHUNT_OHM, DEFAULT_AMP_GAIN);
            sendAdsNotification(ch, raw, mv, ma);
            sendCorsJson(200, "application/json", "{\"status\":\"success\",\"message\":\"Notification triggered for ADS channel\"}");
            return;
        }

        // If a specific sensor requested, send single notification using smoothed/current values
        if (targetIndex >= 0) {
            int pin = getVoltageSensorPin(targetIndex);
            int raw = analogRead(pin);
            float smoothed = getSmoothedADC(targetIndex);
            float calibrated = getSmoothedVoltagePressure(targetIndex);
            // Use calibrated value as the 'voltage' parameter expected by notifier
            sendHttpNotification(targetIndex, raw, smoothed, calibrated);
            sendCorsJson(200, "application/json", "{\"status\":\"success\",\"message\":\"Notification triggered for sensor\"}");
            return;
        }

        // Otherwise trigger a batch notification for all configured sensors
        int n = getNumVoltageSensors();
        int *rawArr = new int[n];
        float *smoothedArr = new float[n];
        int *indices = new int[n];
        for (int i = 0; i < n; ++i) {
            int pin = getVoltageSensorPin(i);
            rawArr[i] = analogRead(pin);
            smoothedArr[i] = getSmoothedADC(i);
            indices[i] = i;
        }
        sendHttpNotificationBatch(n, indices, rawArr, smoothedArr);
        delete[] rawArr;
        delete[] smoothedArr;
        delete[] indices;
        sendCorsJson(200, "application/json", "{\"status\":\"success\",\"message\":\"Batch notification triggered\"}");
    });
    // Time config: allow enabling/disabling RTC while keeping NTP
    server->on("/api/time/config", HTTP_GET, []() {
        bool rtcEnabled = getRtcEnabled();
    JsonDocument doc;
        doc["rtc_enabled"] = rtcEnabled ? 1 : 0;
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    server->on("/api/time/config", HTTP_POST, []() {
    if (!server->hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing body\"}"); return; }
        String body = server->arg("plain");
    JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
    if (err) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}"); return; }
        bool rtcEnabled = doc["rtc_enabled"].as<int>() != 0;
        setRtcEnabled(rtcEnabled);
        // If enabling RTC and RTC present and we have time, update RTC from system time
        if (rtcEnabled && isRtcPresent()) {
            time_t nowEpoch = time(nullptr);
            struct tm *tm_now = localtime(&nowEpoch);
            if (tm_now) {
                ::rtc.adjust(DateTime(tm_now->tm_year + 1900, tm_now->tm_mon + 1, tm_now->tm_mday, tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec));
            }
        }
        sendCorsJson(200, "application/json", "{\"status\":\"success\",\"message\":\"Time config updated\"}");
    });
    // SD config: enable/disable SD logging while keeping webhook notifications
    server->on("/api/sd/config", HTTP_GET, []() {
    bool sd = getSdEnabled();
    JsonDocument doc;
    doc["sd_enabled"] = sd ? 1 : 0;
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    server->on("/api/sd/config", HTTP_POST, []() {
    if (!server->hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing body\"}"); return; }
        String body = server->arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}"); return; }
        bool sd = doc["sd_enabled"].as<int>() != 0;
        setSdEnabled(sd);
        sendCorsJson(200, "application/json", "{\"status\":\"success\",\"message\":\"SD config updated\"}");
    });
    // Global OPTIONS handler for CORS preflight
    server->on("*", HTTP_OPTIONS, []() {
        setCorsHeaders();
        sendCorsJson(204, "text/plain", "");
    });

    // Serve Swagger UI static files from SD under /swagger
    // Expect SD to contain a `docs` folder with `index.html` and `openapi.yaml`.
    server->on("/swagger", HTTP_GET, []() {
        // redirect to the index under /swagger/
        server->sendHeader("Location", "/swagger/", true);
        sendCorsJson(302, "text/plain", "");
    });

    server->on("/swagger/", HTTP_GET, []() {
        // Serve /docs/index.html from SD
        if (!sdReady) { sendCorsJson(404, "text/plain", "SD not available"); return; }
        if (!SD.exists("/docs/index.html") && !SD.exists("/docs/redoc.html") ) { sendCorsJson(404, "text/plain", "swagger index not found on SD"); return; }
        // Prefer a bundled Redoc if present; otherwise serve index.html
        if (SD.exists("/docs/redoc.html") || SD.exists("/docs/redoc.html.gz") ) {
            if (streamSdFileWithGzip(String("/docs/redoc.html"), "text/html")) return;
        }
        if (streamSdFileWithGzip(String("/docs/index.html"), "text/html")) return;
        sendCorsJson(500, "text/plain", "Failed to stream swagger index/redoc");
    });

    // Serve the OpenAPI YAML from SD at /swagger/openapi.yaml
    server->on("/swagger/openapi.yaml", HTTP_GET, []() {
    if (!sdReady) { sendCorsJson(404, "text/plain", "SD not available"); return; }
    if (!SD.exists("/docs/openapi.yaml") && !SD.exists("/docs/openapi.yaml.gz")) { sendCorsJson(404, "text/plain", "openapi.yaml not found on SD"); return; }
    if (streamSdFileWithGzip(String("/docs/openapi.yaml"), "application/x-yaml")) return;
    sendCorsJson(500, "text/plain", "Failed to stream openapi.yaml");
    });
    server->begin();
    webServerPort = port;
    Serial.printf("Web server started on port %d\n", webServerPort);
    // Advertise the HTTP service on the actual port so clients can discover it
    MDNS.addService("http", "tcp", webServerPort);
}

// forward declaration
void handleTime();

void handleTime() {
    JsonDocument doc;
    time_t sysEpoch = time(nullptr);
    doc["system_epoch"] = (unsigned long)sysEpoch;
    doc["system_iso"] = formatIsoWithTz(sysEpoch);

    if (isRtcPresent()) {
        time_t rtcEpoch = getRtcEpoch();
        doc["rtc_epoch"] = (unsigned long)rtcEpoch;
        doc["rtc_iso"] = formatIsoWithTz(rtcEpoch);
    } else {
        doc["rtc_epoch"] = 0;
        doc["rtc_iso"] = "";
    }

    doc["last_ntp_epoch"] = (unsigned long)getLastNtpSuccessEpoch();
    doc["pending_rtc_sync"] = isPendingRtcSync();

    String response;
        serializeJson(doc, response);
        sendCorsJson(200, "application/json", response);
}

void handleWebServerClients() {
    if (server) server->handleClient();
}
