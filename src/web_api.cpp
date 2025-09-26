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
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <ArduinoJson.h>
#pragma GCC diagnostic pop
#include <Preferences.h> // For persistent storage
#include "preferences_helper.h"
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
Preferences preferences; // For NVS Flash

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
    preferences.begin(PREF_NAMESPACE, false);
    // preferences.putString(PREF_HTTP_URL, HTTP_NOTIFICATION_URL); // Example: Save HTTP URL
    // Save other config parameters here
    preferences.end();
    Serial.println("Configuration saved.");
}

void loadConfig() {
    preferences.begin(PREF_NAMESPACE, false); // Open writable to avoid NOT_FOUND when namespace missing
    // For macros, you'd need to read into a global variable or pass around.
    // For now, we'll just print it.
    Serial.print("Loaded HTTP URL: ");
    Serial.println(preferences.getString(PREF_HTTP_URL, "N/A"));
    preferences.end();
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

                JsonDocDyn doc(1024);
        DeserializationError error = deserializeJson(doc, body);

        if (error) {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.f_str());
            sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
            return;
        }

        // Example: Update HTTP_NOTIFICATION_URL (requires a non-macro variable)
        // For now, just acknowledge receipt
        if (doc["http_notification_url"].is<String>()) {
            Serial.print("New HTTP Notification URL: ");
            Serial.println(doc["http_notification_url"].as<String>());
            // saveConfig(); // Call save config after updating variables
        }

        sendCorsJson(200, "application/json", "{\"status\":\"success\", \"message\":\"Config received\"}");
    } else {
        // Handle GET request for current config
            JsonDocDyn doc(1024);
        // doc["http_notification_url"] = HTTP_NOTIFICATION_URL; // Example: Send current URL
        // Add other config parameters to send

        String response;
        serializeJson(doc, response);
        sendCorsJson(200, "application/json", response);
    }
}

void loadCalibration() {
    preferences.begin(CAL_NAMESPACE, false);
    // Load calibration values
    float zeroRawAdc = preferences.getFloat(CAL_ZERO_RAW_ADC, 0.0);
    float spanRawAdc = preferences.getFloat(CAL_SPAN_RAW_ADC, 0.0);
    float zeroPressureValue = preferences.getFloat(CAL_ZERO_PRESSURE_VALUE, 0.0);
    float spanPressureValue = preferences.getFloat(CAL_SPAN_PRESSURE_VALUE, 0.0);
    preferences.end();

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

                JsonDocDyn doc(1024);
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
            JsonDocDyn doc(1024);
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

    server->on("/time/sync", HTTP_POST, []() {
        // Trigger immediate NTP sync and request RTC update if RTC present
        syncNtp(isRtcPresent());
        sendCorsJson(200, "application/json", "{\"status\":\"ok\", \"message\":\"NTP sync triggered\"}");
    });
    server->on("/time/status", HTTP_GET, []() {
    JsonDocDyn doc(1024);
        doc["rtc_found"] = isRtcPresent() ? 1 : 0;
        doc["rtc_lost_power"] = isRtcLostPower() ? 1 : 0;
        time_t rtcEpoch = isRtcPresent() ? getRtcEpoch() : 0;
        doc["rtc_epoch"] = (unsigned long)rtcEpoch;
        doc["rtc_iso"] = isRtcPresent() ? String(ctime(&rtcEpoch)) : String("");
        time_t sysEpoch = time(nullptr);
        doc["system_epoch"] = (unsigned long)sysEpoch;
        doc["system_iso"] = String(ctime(&sysEpoch));
        doc["last_ntp_epoch"] = (unsigned long)getLastNtpSuccessEpoch();
        doc["last_ntp_iso"] = getLastNtpSuccessIso();
        doc["pending_rtc_sync"] = isPendingRtcSync() ? 1 : 0;
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    // Read a single sensor by tag (supports query ?tag=AI1 or path /tag/AI1)
    // Default sampling is 100; can be overridden with ?sampling=N
    server->on("/tag", HTTP_GET, []() {
        String tag;
        if (server->hasArg("tag")) {
            tag = server->arg("tag");
        } else {
            // no tag query; require path style /tag/<TAG>
            String uri = server->uri();
            if (uri.startsWith("/tag/")) tag = uri.substring(5);
        }

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
            int raw = analogRead(pin);
            sum += (unsigned long)raw;
            delay(1);
        }
    float avgRaw = ((float)sum) / ((float)sampling);
        struct SensorCalibration cal = getCalibrationForPin(pinIndex);
        float converted = (avgRaw * cal.scale) + cal.offset;

        JsonDocDyn outDoc(256);
        outDoc["tag"] = tag;
        outDoc["pin_index"] = pinIndex;
        outDoc["pin"] = pin;
        outDoc["samples"] = sampling;
        outDoc["measured_raw_avg"] = avgRaw;
        JsonObject conv = outDoc["converted"].to<JsonObject>();
        conv["value"] = roundToDecimals(converted, 2);
        conv["unit"] = "bar";

        String out;
        serializeJson(outDoc, out);
        sendCorsJson(200, "application/json", out);
    });

    // Provide support for path-style /tag/AI1 by routing through notFound handler
    server->onNotFound([]() {
        String uri = server->uri();
        if (!uri.startsWith("/tag/")) {
            // default 404
            sendCorsJson(404, "application/json", "{\"status\":\"error\",\"message\":\"Not found\"}");
            return;
        }

        String tag = uri.substring(5);
        int sampling = 100;
        if (server->hasArg("sampling")) sampling = server->arg("sampling").toInt();
        if (sampling <= 0) sampling = 100;

        int pinIndex = tagToIndex(tag);
        if (pinIndex < 0) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Unknown tag\"}");
            return;
        }

        int pin = getVoltageSensorPin(pinIndex);
        unsigned long sum = 0;
        for (int i = 0; i < sampling; ++i) {
            int raw = analogRead(pin);
            sum += (unsigned long)raw;
            delay(1);
        }
    float avgRaw = ((float)sum) / ((float)sampling);
        struct SensorCalibration cal = getCalibrationForPin(pinIndex);
        float converted = (avgRaw * cal.scale) + cal.offset;

        JsonDocDyn outDoc(256);
        outDoc["tag"] = tag;
        outDoc["pin_index"] = pinIndex;
        outDoc["pin"] = pin;
        outDoc["samples"] = sampling;
        outDoc["measured_raw_avg"] = avgRaw;
        JsonObject conv = outDoc["converted"].to<JsonObject>();
        conv["value"] = roundToDecimals(converted, 2);
        conv["unit"] = "bar";

        String out;
        serializeJson(outDoc, out);
        sendCorsJson(200, "application/json", out);
    });
    // RTC read/set endpoints
    server->on("/time/rtc", HTTP_GET, []() {
        JsonDocDyn doc(256);
        doc["rtc_found"] = isRtcPresent() ? 1 : 0;
        doc["rtc_lost_power"] = isRtcLostPower() ? 1 : 0;
        time_t rtcEpoch = isRtcPresent() ? getRtcEpoch() : 0;
        doc["rtc_epoch"] = (unsigned long)rtcEpoch;
        doc["rtc_iso"] = isRtcPresent() ? String(ctime(&rtcEpoch)) : String("");
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    server->on("/time/rtc", HTTP_POST, []() {
        if (!server->hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing body\"}"); return; }
        String body = server->arg("plain");
        JsonDocDyn doc(256);
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
    JsonDocDyn doc(body.length() + 1);
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

            JsonDocDyn respDoc(256);
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
            JsonDocDyn doc(256);
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
    JsonDocDyn doc(1024);
        DeserializationError err = deserializeJson(doc, body);
        if (err) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}"); return; }

        // Build list of targets: map pinNumber -> targetPressure
        std::map<int, float> targets;
        if (!doc["sensors"].isNull() && doc["sensors"].is<JsonArray>()) {
            for (JsonObject so : doc["sensors"].as<JsonArray>()) {
                int pin = -1;
                if (!so["pin"].isNull()) pin = so["pin"].as<int>();
                else if (!so["tag"].isNull()) pin = tagToIndex(so["tag"].as<String>());
                if (pin >= 0 && !so["target"].isNull()) {
                    float t = (float)so["target"].as<float>();
                    targets[pin] = t;
                }
            }
    } else if (!doc["target"].isNull() && (doc["target"].is<float>() || doc["target"].is<int>())) {
            float t = doc["target"].is<float>() ? doc["target"].as<float>() : (float)doc["target"].as<int>();
            int n = getNumVoltageSensors();
            for (int i = 0; i < n; ++i) {
                targets[getVoltageSensorPin(i)] = t;
            }
        } else {
            sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"No target provided\"}");
            return;
        }

    JsonDocDyn resp(512);
    JsonArray results = resp["results"].to<JsonArray>();

        for (auto const& kv : targets) {
            int pin = kv.first;
            float targetPressure = kv.second;
            int pinIndex = findVoltageSensorIndexByPin(pin);
            if (pinIndex < 0) {
                JsonObject r = results.add<JsonObject>();
                r["pin"] = pin;
                r["status"] = "error";
                r["message"] = "unknown pin";
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
    JsonDocDyn doc(1024);
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

    JsonDocDyn resp(512);
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
            Preferences pcal;
            pcal.begin(CAL_NAMESPACE, false);
            pcal.putFloat(key, new_tp_scale);
            pcal.end();

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
    JsonDocDyn doc(1024);
        DeserializationError err = deserializeJson(doc, body);
        if (err) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}"); return; }

        // Build list of targets: map pinNumber -> targetPressure
        std::map<int, float> targets; // pinNumber -> pressure

        if (!doc["sensors"].isNull() && doc["sensors"].is<JsonArray>()) {
            for (JsonObject so : doc["sensors"].as<JsonArray>()) {
                int pin = -1;
                if (!so["pin"].isNull()) pin = so["pin"].as<int>();
                else if (!so["tag"].isNull()) pin = tagToIndex(so["tag"].as<String>());
                if (pin >= 0 && !so["target"].isNull()) {
                    float t = (float)so["target"].as<float>();
                    targets[pin] = t;
                }
            }
    } else if (!doc["target"].isNull() && (doc["target"].is<float>() || doc["target"].is<int>())) {
            float t = doc["target"].is<float>() ? doc["target"].as<float>() : (float)doc["target"].as<int>();
            // apply to all ADC sensors
            int n = getNumVoltageSensors();
            for (int i = 0; i < n; ++i) {
                targets[getVoltageSensorPin(i)] = t;
            }
        } else {
            sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"No target provided\"}");
            return;
        }

    JsonDocDyn resp(512);
    JsonArray results = resp["results"].to<JsonArray>();

        // For each target, measure current smoothed ADC and save as span
        for (auto const& kv : targets) {
            int pin = kv.first;
            float targetPressure = kv.second;
            int pinIndex = findVoltageSensorIndexByPin(pin);
            if (pinIndex < 0) {
                JsonObject r = results.add<JsonObject>();
                r["pin"] = pin;
                r["status"] = "error";
                r["message"] = "unknown pin";
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
    JsonDocDyn doc(1024);
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
    JsonDocDyn doc(1024);
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
    JsonDocDyn doc(256);
        DeserializationError err = deserializeJson(doc, body);
        if (err) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\": \"Invalid JSON\"}");
            return;
        }

        if (!doc["sensors"].is<JsonArray>()) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\": \"Missing sensors array\"}");
            return;
        }

        Preferences p;
        p.begin("sensors", false);

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
            p.putInt(enKey.c_str(), enabled ? 1 : 0);
            p.putULong(ivKey.c_str(), interval);
        }
        p.end();

        sendCorsJson(200, "application/json", "{\"status\":\"success\", \"message\":\"Sensor config updated\"}");
    });
    // Live sensor readings: raw ADC (current analogRead), smoothed ADC, and calibrated voltage
    server->on("/sensors/readings", HTTP_GET, []() {
        int n = getNumVoltageSensors();
        const int ads_channels = 2; // currently reporting ADS1115 ch 0..1
        String fromUnit = "";
        if (server->hasArg("convert_from")) fromUnit = server->arg("convert_from");
    JsonDocDyn doc(256);
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
    Preferences p;
    p.begin(PREF_NAMESPACE, false);
    int mode = p.getInt(PREF_NOTIFICATION_MODE, DEFAULT_NOTIFICATION_MODE);
    int payload = p.getInt(PREF_NOTIFICATION_PAYLOAD, DEFAULT_NOTIFICATION_PAYLOAD_TYPE);
    p.end();
    JsonDocDyn doc(512);
    doc["mode"] = mode;
    doc["payload_type"] = payload;
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    server->on("/notifications/config", HTTP_POST, []() {
    if (!server->hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing body\"}"); return; }
    String body = server->arg("plain");
    JsonDocDyn doc(256);
    DeserializationError err = deserializeJson(doc, body);
    if (err) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}"); return; }

        int mode = doc["mode"].is<int>() ? doc["mode"].as<int>() : DEFAULT_NOTIFICATION_MODE;
        int payload = doc["payload_type"].is<int>() ? doc["payload_type"].as<int>() : DEFAULT_NOTIFICATION_PAYLOAD_TYPE;

        Preferences p;
        p.begin(PREF_NAMESPACE, false);
        p.putInt(PREF_NOTIFICATION_MODE, mode);
        p.putInt(PREF_NOTIFICATION_PAYLOAD, payload);
        p.end();

        setNotificationMode((uint8_t)mode);
        setNotificationPayloadType((uint8_t)payload);

        sendCorsJson(200, "application/json", "{\"status\":\"success\",\"message\":\"Notification config updated\"}");
    });
    // ADS channel configuration endpoints: view and set per-channel shunt and amp gain
    server->on("/ads/config", HTTP_GET, []() {
    JsonDocDyn doc(512);
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
        Preferences p2;
        p2.begin("ads_cfg", false);
        float ema = safeGetFloat(p2, "ema_alpha", 0.1f);
        int numavg = p2.getInt("num_avg", 5);
        p2.end();
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
    JsonDocDyn doc(512);
        DeserializationError err = deserializeJson(doc, body);
        if (err) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\": \"Invalid JSON\"}");
            return;
        }
        if (!doc["channels"].is<JsonArray>()) { sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\": \"Missing channels array\"}"); return; }
        Preferences p;
        p.begin("ads_cfg", false);
        for (JsonObject chObj : doc["channels"].as<JsonArray>()) {
            int ch = chObj["channel"].as<int>();
            if (ch < 0 || ch > 3) continue;
            if (!chObj["shunt_ohm"].isNull()) {
                char key[16]; snprintf(key, sizeof(key), "shunt_%d", ch);
                p.putFloat(key, (float)chObj["shunt_ohm"].as<float>());
            }
            if (!chObj["amp_gain"].isNull()) {
                char key[16]; snprintf(key, sizeof(key), "amp_%d", ch);
                p.putFloat(key, (float)chObj["amp_gain"].as<float>());
            }
            if (!chObj["ads_mode"].isNull()) {
                char key[16]; snprintf(key, sizeof(key), "mode_%d", ch);
                p.putInt(key, (int)chObj["ads_mode"].as<int>());
            }
            if (!chObj["tp_scale_mv_per_ma"].isNull()) {
                char key[16]; snprintf(key, sizeof(key), "tp_scale_%d", ch);
                Preferences pcal;
                pcal.begin(CAL_NAMESPACE, false);
                pcal.putFloat(key, (float)chObj["tp_scale_mv_per_ma"].as<float>());
                pcal.end();
            }
        }
        if (!doc["ema_alpha"].isNull()) {
            float ema = (float)doc["ema_alpha"].as<float>();
            p.putFloat("ema_alpha", ema);
            setAdsEmaAlpha(ema);
        }
        if (!doc["num_avg"].isNull()) {
            int na = (int)doc["num_avg"].as<int>();
            p.putInt("num_avg", na);
            setAdsNumAvg(na);
        }
        p.end();
        sendCorsJson(200, "application/json", "{\"status\":\"success\", \"message\":\"ADS config saved\"}");
    });

    // ADC smoothing/runtime sample-store configuration
    server->on("/adc/config", HTTP_GET, []() {
    JsonDocDyn doc(128);
        doc["adc_num_samples"] = getAdcNumSamples();
        doc["samples_per_sensor"] = getSampleCapacity();
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    server->on("/adc/config", HTTP_POST, []() {
        if (!server->hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing body\"}"); return; }
        String body = server->arg("plain");
    JsonDocDyn doc(256);
        DeserializationError err = deserializeJson(doc, body);
        if (err) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}"); return; }
        bool changed = false;
    if (!doc["adc_num_samples"].isNull()) {
            int ns = doc["adc_num_samples"].as<int>();
            setAdcNumSamples(ns);
            // Persist adc_num_samples so it survives reboot
            Preferences pns;
            pns.begin("adc_cfg", false);
            pns.putInt("num_samples", ns);
            pns.end();
            changed = true;
        }
    if (!doc["samples_per_sensor"].isNull()) {
            int sp = doc["samples_per_sensor"].as<int>();
            resizeSampleStore(sp);
            // Persist the chosen capacity so reboots preserve it
            Preferences p;
            p.begin("adc_cfg", false);
            p.putInt("samples_per_sensor", sp);
            p.end();
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
    JsonDocDyn doc(256);
        DeserializationError err = deserializeJson(doc, body);
        if (err) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}"); return; }
        if (!doc["channels"].is<JsonArray>()) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing channels array\"}"); return; }
        Preferences p;
        p.begin("ads_cfg", false);
        for (JsonObject chObj : doc["channels"].as<JsonArray>()) {
            int ch = chObj["channel"].as<int>();
            if (ch < 0 || ch > 3) continue;
            if (!chObj["shunt_ohm"].isNull()) {
                char key[16]; snprintf(key, sizeof(key), "shunt_%d", ch);
                p.putFloat(key, (float)chObj["shunt_ohm"].as<float>());
            }
            if (!chObj["amp_gain"].isNull()) {
                char key[16]; snprintf(key, sizeof(key), "amp_%d", ch);
                p.putFloat(key, (float)chObj["amp_gain"].as<float>());
            }
            if (!chObj["ads_mode"].isNull()) {
                char key[16]; snprintf(key, sizeof(key), "mode_%d", ch);
                p.putInt(key, (int)chObj["ads_mode"].as<int>());
            }
            if (!chObj["tp_scale_mv_per_ma"].isNull()) {
                char key[16]; snprintf(key, sizeof(key), "tp_scale_%d", ch);
                // Persist tp_scale into calibration namespace for unified access
                Preferences pcal;
                pcal.begin(CAL_NAMESPACE, false);
                pcal.putFloat(key, (float)chObj["tp_scale_mv_per_ma"].as<float>());
                pcal.end();
            }
        }
        // Optionally accept smoothing params at top level
    if (!doc["ema_alpha"].isNull()) {
            float ema = (float)doc["ema_alpha"].as<float>();
            p.putFloat("ema_alpha", ema);
            setAdsEmaAlpha(ema);
        }
    if (!doc["num_avg"].isNull()) {
            int na = (int)doc["num_avg"].as<int>();
            p.putInt("num_avg", na);
            setAdsNumAvg(na);
        }
        p.end();
        sendCorsJson(200, "application/json", "{\"status\":\"success\",\"message\":\"ADS config saved\"}");
    });
    // Trigger notification(s) on demand: POST body may include { "sensor_index": int } or { "pin": int } (accepts legacy "pin_number" too)
    server->on("/notifications/trigger", HTTP_POST, []() {
    if (!server->hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing body\"}"); return; }
        String body = server->arg("plain");
    JsonDocDyn doc(256);
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
    JsonDocDyn doc(128);
        doc["rtc_enabled"] = rtcEnabled ? 1 : 0;
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    server->on("/time/config", HTTP_POST, []() {
    if (!server->hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing body\"}"); return; }
        String body = server->arg("plain");
    JsonDocDyn doc(128);
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
    JsonDocDyn doc(128);
    doc["sd_enabled"] = sd ? 1 : 0;
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    server->on("/sd/config", HTTP_POST, []() {
    if (!server->hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing body\"}"); return; }
        String body = server->arg("plain");
    JsonDocDyn doc(128);
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
    server->begin();
    webServerPort = port;
    Serial.printf("Web server started on port %d\n", webServerPort);
    // Advertise the HTTP service on the actual port so clients can discover it
    MDNS.addService("http", "tcp", webServerPort);
}

// forward declaration
void handleTime();

void handleTime() {
    JsonDocDyn doc(256);
    time_t sysEpoch = time(nullptr);
    doc["system_epoch"] = (unsigned long)sysEpoch;
    doc["system_iso"] = ctime(&sysEpoch);

    if (isRtcPresent()) {
        time_t rtcEpoch = getRtcEpoch();
        doc["rtc_epoch"] = (unsigned long)rtcEpoch;
        doc["rtc_iso"] = ctime(&rtcEpoch);
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
