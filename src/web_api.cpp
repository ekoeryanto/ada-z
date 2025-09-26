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
    Serial.println("Configuration saved.");
}

void loadConfig() {
    // Read persisted values from NVS and print for debug
    String httpUrl = loadStringFromNVSns(PREF_NAMESPACE, PREF_HTTP_URL, String("N/A"));
    Serial.print("Loaded HTTP URL: ");
    Serial.println(httpUrl);
    Serial.println("Configuration loaded.");
}

void handleConfig() {
    if (server->method() == HTTP_POST) {
        if (!server->hasArg("plain")) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Missing JSON body\"}");
            return;
        }

        String body = server->arg("plain");
        Serial.print("Received config POST: ");
        Serial.println(body);

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
            Serial.print("New HTTP Notification URL: ");
            Serial.println(doc["http_notification_url"].as<String>());
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

    Serial.println("Calibration loaded:");
    Serial.printf("  Zero Raw ADC: %.2f\n", zeroRawAdc);
    Serial.printf("  Span Raw ADC: %.2f\n", spanRawAdc);
    Serial.printf("  Zero Pressure Value: %.2f\n", zeroPressureValue);
    Serial.printf("  Span Pressure Value: %.2f\n", spanPressureValue);
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

void handleCalibrate() {
    if (server->method() == HTTP_POST) {
        if (!server->hasArg("plain")) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Missing JSON body\"}");
            return;
        }

        String body = server->arg("plain");
        Serial.print("Received calibrate POST: ");
        Serial.println(body);

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
            Serial.printf("Calibration saved for pin index %d\n", pinIndex);
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
        Serial.println("SD initialized for static file serving.");
    } else {
        Serial.println("SD not available at startup; /swagger endpoints disabled");
    }

    server->on("/time/sync", HTTP_POST, []() {
        // Trigger immediate NTP sync and request RTC update if RTC present
        syncNtp(isRtcPresent());
        sendCorsJson(200, "application/json", "{\"status\":\"ok\", \"message\":\"NTP sync triggered\"}");
    });
    // Expose a generic config endpoint to GET/POST small config (persisted to NVS)
    server->on("/config", HTTP_ANY, []() {
        handleConfig();
    });
    server->on("/time/status", HTTP_GET, []() {
    JsonDocument doc;
        doc["rtc_found"] = isRtcPresent() ? 1 : 0;
        doc["rtc_lost_power"] = isRtcLostPower() ? 1 : 0;
        time_t rtcEpoch = isRtcPresent() ? getRtcEpoch() : 0;
        char isoBuf[32];
        doc["rtc_epoch"] = (unsigned long)rtcEpoch;
        if (isRtcPresent()) {
            struct tm *tm_rtc = gmtime(&rtcEpoch);
            strftime(isoBuf, sizeof(isoBuf), "%Y-%m-%dT%H:%M:%SZ", tm_rtc);
            doc["rtc_iso"] = String(isoBuf);
        } else {
            doc["rtc_iso"] = "";
        }
        time_t sysEpoch = time(nullptr);
        struct tm *tm_sys = gmtime(&sysEpoch);
        doc["system_epoch"] = (unsigned long)sysEpoch;
        strftime(isoBuf, sizeof(isoBuf), "%Y-%m-%dT%H:%M:%SZ", tm_sys);
        doc["system_iso"] = String(isoBuf);
        doc["last_ntp_epoch"] = (unsigned long)getLastNtpSuccessEpoch();
        doc["last_ntp_iso"] = getLastNtpSuccessIso();
        doc["pending_rtc_sync"] = isPendingRtcSync() ? 1 : 0;
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    // Helper function to handle tag-based sensor reading logic
    auto handleTagRead = [](const String& tag) {
        int sampling = 100;
        if (server->hasArg("sampling")) sampling = server->arg("sampling").toInt();
        if (sampling <= 0) sampling = 100;

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
        unsigned long sum = 0;
        for (int i = 0; i < sampling; ++i) {
            sum += (unsigned long)analogRead(pin);
            delay(1);
        }
        float avgRaw = ((float)sum) / ((float)sampling);
        struct SensorCalibration cal = getCalibrationForPin(pinIndex);
        float converted = (avgRaw * cal.scale) + cal.offset;

        JsonDocument outDoc;
        outDoc["tag"] = tag;
        outDoc["pin_index"] = pinIndex;
        outDoc["pin"] = pin;
        outDoc["samples"] = sampling;
        outDoc["measured_raw_avg"] = avgRaw;
        outDoc["converted"]["value"] = roundToDecimals(converted, 2);
        outDoc["converted"]["unit"] = "bar";

        String out;
        serializeJson(outDoc, out);
        sendCorsJson(200, "application/json", out);
    };

    // Read a single sensor by tag (supports query ?tag=AI1 or path /tag/AI1)
    // Default sampling is 100; can be overridden with ?sampling=N
    server->on("/tag", HTTP_GET, [handleTagRead]() {
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
        if (!uri.startsWith("/tag/")) {
            // default 404
            sendCorsJson(404, "application/json", "{\"status\":\"error\",\"message\":\"Not found\"}");
            return;
        }
        String tag = uri.substring(5);
        handleTagRead(tag);
    });
    // Secure OTA update endpoint (multipart/form-data upload)
    server->on("/update", HTTP_POST, []() {
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
    server->on("/time/rtc", HTTP_GET, []() {
        JsonDocument doc;
        doc["rtc_found"] = isRtcPresent() ? 1 : 0;
        doc["rtc_lost_power"] = isRtcLostPower() ? 1 : 0;
        time_t rtcEpoch = isRtcPresent() ? getRtcEpoch() : 0;
        char isoBuf[32];
        doc["rtc_epoch"] = (unsigned long)rtcEpoch;
        if (isRtcPresent()) {
            struct tm *tm_rtc = gmtime(&rtcEpoch);
            strftime(isoBuf, sizeof(isoBuf), "%Y-%m-%dT%H:%M:%SZ", tm_rtc);
            doc["rtc_iso"] = String(isoBuf);
        } else {
            doc["rtc_iso"] = "";
        }
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    server->on("/time/rtc", HTTP_POST, []() {
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
    server->on("/calibrate/pin", HTTP_POST, []() {
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

        // Blocking sampled calibration: if caller provides a `value` (target pressure),
        // take N analog samples (averaged) and use that averaged raw as the span point.
        if (!doc["value"].isNull() && (doc["value"].is<float>() || doc["value"].is<int>())) {
            float targetPressure = doc["value"].is<float>() ? doc["value"].as<float>() : (float)doc["value"].as<int>();
            int samples = !doc["samples"].isNull() ? doc["samples"].as<int>() : 15;
            int sample_delay_ms = !doc["sample_delay_ms"].isNull() ? doc["sample_delay_ms"].as<int>() : 50;

            unsigned long sum = 0;
            for (int i = 0; i < samples; ++i) {
                int raw = analogRead(getVoltageSensorPin(pinIndex));
                sum += (unsigned long)raw;
                delay(sample_delay_ms);
            }
            float avgRaw = ((float)sum) / ((float)samples);

            struct SensorCalibration cal = getCalibrationForPin(pinIndex);
            // Save calibration: keep existing zero, set span to measured avg with provided pressure
            saveCalibrationForPin(pinIndex, cal.zeroRawAdc, avgRaw, cal.zeroPressureValue, targetPressure);
            // Reseed smoothed ADC so readings update immediately
            setupVoltagePressureSensor();

            JsonDocument respDoc;
            respDoc["status"] = "success";
            respDoc["message"] = "Blocking span calibration applied";
            respDoc["pin_index"] = pinIndex;
            respDoc["pin"] = getVoltageSensorPin(pinIndex);
            respDoc["measured_raw_avg"] = avgRaw;
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
    server->on("/calibrate/default", HTTP_POST, []() {
        int n = getNumVoltageSensors();
        for (int i = 0; i < n; ++i) {
            saveCalibrationForPin(i, 0.0f, 4095.0f, 0.0f, 10.0f);
        }
        // Reseed smoothed ADCs so readings update immediately after calibration
        setupVoltagePressureSensor();
        sendCorsJson(200, "application/json", "{\"status\":\"success\", \"message\":\"Default calibration applied to all sensors\"}");
    });

    // Convenience: apply default calibration for a single pin by pin
    server->on("/calibrate/default/pin", HTTP_POST, []() {
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
    server->on("/adc/calibrate/pin", HTTP_ANY, []() {
        handleCalibrate();
    });

    // Auto-calibration under /adc prefix: apply span using current smoothed ADC readings
    server->on("/adc/calibrate/auto", HTTP_POST, []() {
        if (!server->hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing body\"}"); return; }
        String body = server->arg("plain");
    JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (err) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}"); return; }

        // Build list of targets: map sensor index -> targetPressure
        JsonDocument resp;
        JsonArray results = resp["results"].to<JsonArray>();
        std::map<int, float> targets;

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
            float smoothed = getSmoothedADC(pinIndex);
            float spanRaw = round(smoothed);
            struct SensorCalibration cal = getCalibrationForPin(pinIndex);
            saveCalibrationForPin(pinIndex, cal.zeroRawAdc, spanRaw, cal.zeroPressureValue, targetPressure);
            setupVoltagePressureSensor();

            JsonObject r = results.add<JsonObject>();
            r["pin"] = pin;
            r["pin_index"] = pinIndex;
            r["measured_smoothed"] = smoothed;
            r["applied_span_raw"] = spanRaw;
            r["span_pressure_value"] = targetPressure;
            r["status"] = "applied";
        }

        String out;
        serializeJson(resp, out);
        sendCorsJson(200, "application/json", out);
    });

    // SD error log endpoints
    server->on("/sd/error_log", HTTP_GET, []() {
        int lines = -1;
        if (server->hasArg("lines")) lines = server->arg("lines").toInt();
        String content = readErrorLog(lines);
        if (content.length() == 0) {
            sendCorsJson(200, "text/plain", "");
            return;
        }
        sendCorsJson(200, "text/plain", content);
    });

    server->on("/sd/error_log/clear", HTTP_POST, []() {
        clearErrorLog();
        sendCorsJson(200, "application/json", "{\"status\":\"success\",\"message\":\"error log cleared\"}");
    });
    // ADS auto-calibration: compute and persist tp_scale (mV per mA) so TP5551-derived voltage maps to target pressure
    server->on("/ads/calibrate/auto", HTTP_POST, []() {
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
    server->on("/calibrate/auto", HTTP_POST, []() {
        if (!server->hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing body\"}"); return; }
        String body = server->arg("plain");
    JsonDocument doc;
        DeserializationError err = deserializeJson(doc, body);
        if (err) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}"); return; }

        // Build list of targets: map sensor index -> targetPressure
        JsonDocument resp;
        JsonArray results = resp["results"].to<JsonArray>();
        std::map<int, float> targets; // sensorIndex -> pressure

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

            // read smoothed ADC (current averaged value)
            float smoothed = getSmoothedADC(pinIndex);
            // use round(smoothed) as raw span
            float spanRaw = round(smoothed);
            struct SensorCalibration cal = getCalibrationForPin(pinIndex);
            saveCalibrationForPin(pinIndex, cal.zeroRawAdc, spanRaw, cal.zeroPressureValue, targetPressure);

            // reseed sensor smoothing so values update immediately
            setupVoltagePressureSensor();

            JsonObject r = results.add<JsonObject>();
            r["pin"] = pin;
            r["pin_index"] = pinIndex;
            r["measured_smoothed"] = smoothed;
            r["applied_span_raw"] = spanRaw;
            r["span_pressure_value"] = targetPressure;
            r["status"] = "applied";
        }

        String out;
        serializeJson(resp, out);
        sendCorsJson(200, "application/json", out);
    });
    server->on("/calibrate", handleCalibrate);
    server->on("/calibrate/all", HTTP_GET, []() {
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
    server->on("/sensors/config", HTTP_GET, []() {
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

    server->on("/sensors/config", HTTP_POST, []() {
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
    server->on("/sensors/readings", HTTP_GET, []() {
        int n = getNumVoltageSensors();
        const int ads_channels = 2; // currently reporting ADS1115 ch 0..1
        String fromUnit = "";
        if (server->hasArg("convert_from")) fromUnit = server->arg("convert_from");
    JsonDocument doc;
        // Add normalized ISO timestamp using time_sync helper
        doc["timestamp"] = getIsoTimestamp();
        // Expose the RTU at top-level and create a flat `tags` array of sensor objects
        String rtuId = String(getChipId());
        doc["rtu"] = rtuId;
    JsonArray tags = doc["tags"].to<JsonArray>();

        // First, collect ADC sensors grouped by RTU
        for (int i = 0; i < n; ++i) {
            int pin = getVoltageSensorPin(i);
            int raw = analogRead(pin);
            float smoothed = getSmoothedADC(i);
            float calibrated = getSmoothedVoltagePressure(i);
            // Prefer averaged values from sample store for API/read responses
            float avgRawF, avgSmoothedF, avgVoltF;
            if (getAverages(i, avgRawF, avgSmoothedF, avgVoltF)) {
                raw = (int)round(avgRawF);
                smoothed = avgSmoothedF;
                calibrated = avgVoltF;
            }
            int raw_original = raw;
            int raw_effective = raw;
            bool saturated = isPinSaturated(i);
            if (raw == 4095 && !saturated) raw_effective = (int)round(smoothed);
            float voltage_smoothed_v = convert010V(round(smoothed));
            struct SensorCalibration cal = getCalibrationForPin(i);
            float pressure_from_raw = (raw_effective * cal.scale) + cal.offset;
            float pressure_from_smoothed = (round(smoothed) * cal.scale) + cal.offset;

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
            // ADC: measurement not included; keep scaled/converted only
            JsonObject scaled = val["scaled"].to<JsonObject>();
            scaled["value"] = roundToDecimals(voltage_smoothed_v, 2);
            scaled["unit"] = "volt";
            JsonObject conv = val["converted"].to<JsonObject>();
            conv["value"] = roundToDecimals(calibrated, 2);
            conv["unit"] = "bar";
            conv["semantic"] = "pressure";
            conv["from_raw"] = roundToDecimals(pressure_from_raw, 2);
            conv["from_filtered"] = roundToDecimals(pressure_from_smoothed, 2);

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
            // ADS: move measurement into meta for ADS channels
            JsonObject scaled = val["scaled"].to<JsonObject>();
            scaled["value"] = roundToDecimals(mv / 1000.0f, 2);
            scaled["unit"] = "volt";
            JsonObject convA = val["converted"].to<JsonObject>();
            convA["value"] = roundToDecimals(pressure_bar, 2);
            convA["unit"] = "bar";
            convA["semantic"] = "pressure";
            convA["note"] = "TP5551 derived";
            // Provide from_raw and from_filtered (smoothed) converted pressure for ADS
            convA["from_raw"] = roundToDecimals(pressure_bar, 2);
            float ma_smoothed = getAdsSmoothedMa(ch);
            float mv_from_smoothed = ma_smoothed * tp_scale;
            float voltage_from_smoothed = mv_from_smoothed / 1000.0f;
            float pressure_from_smoothed = (voltage_from_smoothed / 10.0f) * DEFAULT_RANGE_BAR;
            convA["from_filtered"] = roundToDecimals(pressure_from_smoothed, 2);

            JsonObject meta = s["meta"].to<JsonObject>();
            JsonObject meta_meas = meta["measurement"].to<JsonObject>();
            meta_meas["mv"] = mv;
            meta_meas["ma"] = ma;
            meta["tp_model"] = String("TP5551");
            meta["tp_scale_mv_per_ma"] = tp_scale;
            // Also expose tp_scale as a calibration value under cal_* for consistency
            meta["cal_tp_scale_mv_per_ma"] = tp_scale;
            meta["ma_smoothed"] = getAdsSmoothedMa(ch);
            meta["depth_mm"] = depth_mm;
        }

    // tags_total is number of sensor entries returned in `tags`
    doc["tags_total"] = tags.size();

    String resp;
    serializeJson(doc, resp);
    sendCorsJson(200, "application/json", resp);
    });

    // Notification config endpoints
    server->on("/notifications/config", HTTP_GET, []() {
    int mode = loadIntFromNVSns(PREF_NAMESPACE, PREF_NOTIFICATION_MODE, DEFAULT_NOTIFICATION_MODE);
    int payload = loadIntFromNVSns(PREF_NAMESPACE, PREF_NOTIFICATION_PAYLOAD, DEFAULT_NOTIFICATION_PAYLOAD_TYPE);    
    JsonDocument doc;
    doc["mode"] = mode;
    doc["payload_type"] = payload;
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    server->on("/notifications/config", HTTP_POST, []() {
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
    server->on("/ads/config", HTTP_GET, []() {
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
    server->on("/ads/config", HTTP_PUT, []() {
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
    server->on("/adc/config", HTTP_GET, []() {
    JsonDocument doc;
        doc["adc_num_samples"] = getAdcNumSamples();
        doc["samples_per_sensor"] = getSampleCapacity();
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    server->on("/adc/config", HTTP_POST, []() {
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
        if (changed) sendCorsJson(200, "application/json", "{\"status\":\"success\",\"message\":\"ADC config updated\"}");
        else sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"No supported keys provided\"}");
    });

    // Ensure explicit OPTIONS preflight handler for /ads/config
    server->on("/ads/config", HTTP_OPTIONS, []() {
        setCorsHeaders();
        sendCorsJson(204, "text/plain", "");
    });

    server->on("/ads/config", HTTP_POST, []() {
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
    server->on("/notifications/trigger", HTTP_POST, []() {
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
    server->on("/time/config", HTTP_GET, []() {
        bool rtcEnabled = getRtcEnabled();
    JsonDocument doc;
        doc["rtc_enabled"] = rtcEnabled ? 1 : 0;
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    server->on("/time/config", HTTP_POST, []() {
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
    server->on("/sd/config", HTTP_GET, []() {
    bool sd = getSdEnabled();
    JsonDocument doc;
    doc["sd_enabled"] = sd ? 1 : 0;
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    server->on("/sd/config", HTTP_POST, []() {
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
        if (!SD.exists("/docs/index.html")) { sendCorsJson(404, "text/plain", "swagger index not found on SD"); return; }
        File f = SD.open("/docs/index.html", FILE_READ);
        if (!f) { sendCorsJson(500, "text/plain", "Failed to open file"); return; }
        setCorsHeaders();
        server->streamFile(f, "text/html");
        f.close();
    });

    // Serve the OpenAPI YAML from SD at /swagger/openapi.yaml
    server->on("/swagger/openapi.yaml", HTTP_GET, []() {
    if (!sdReady) { sendCorsJson(404, "text/plain", "SD not available"); return; }
    if (!SD.exists("/docs/openapi.yaml")) { sendCorsJson(404, "text/plain", "openapi.yaml not found on SD"); return; }
    File f = SD.open("/docs/openapi.yaml", FILE_READ);
        if (!f) { sendCorsJson(500, "text/plain", "Failed to open openapi.yaml"); return; }
        setCorsHeaders();
        server->streamFile(f, "application/x-yaml");
        f.close();
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
    char isoBuf[32];
    struct tm *tm_sys = gmtime(&sysEpoch);
    strftime(isoBuf, sizeof(isoBuf), "%Y-%m-%dT%H:%M:%SZ", tm_sys);
    doc["system_epoch"] = (unsigned long)sysEpoch;
    doc["system_iso"] = String(isoBuf);

    if (isRtcPresent()) {
        time_t rtcEpoch = getRtcEpoch();
        struct tm *tm_rtc = gmtime(&rtcEpoch);
        strftime(isoBuf, sizeof(isoBuf), "%Y-%m-%dT%H:%M:%SZ", tm_rtc);
        doc["rtc_epoch"] = (unsigned long)rtcEpoch;
        doc["rtc_iso"] = String(isoBuf);
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
