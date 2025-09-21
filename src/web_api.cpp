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
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <ArduinoJson.h>
#pragma GCC diagnostic pop
#include <Preferences.h> // For persistent storage
#include <WiFi.h>
#include <Update.h>
#include <FS.h>
#include <SPI.h>
#include <SD.h>
#include <esp_heap_caps.h>

// ArduinoJson usage updated to recommended APIs

WebServer server(80); // Define WebServer object here
Preferences preferences; // For NVS Flash

// Helper: set common CORS headers for responses
void setCorsHeaders() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// Wrapper to send JSON with CORS headers
void sendCorsJson(int code, const char* contentType, const String &payload) {
    setCorsHeaders();
    server.send(code, contentType, payload);
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
    preferences.begin(PREF_NAMESPACE, true); // Read-only
    // For macros, you'd need to read into a global variable or pass around.
    // For now, we'll just print it.
    Serial.print("Loaded HTTP URL: ");
    Serial.println(preferences.getString(PREF_HTTP_URL, "N/A"));
    preferences.end();
    Serial.println("Configuration loaded.");
}

void handleConfig() {
    if (server.method() == HTTP_POST) {
        if (!server.hasArg("plain")) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Missing JSON body\"}");
            return;
        }

        String body = server.arg("plain");
        Serial.print("Received config POST: ");
        Serial.println(body);

    DynamicJsonDocument doc(512);
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
    DynamicJsonDocument doc(512);
        // doc["http_notification_url"] = HTTP_NOTIFICATION_URL; // Example: Send current URL
        // Add other config parameters to send

        String response;
        serializeJson(doc, response);
        sendCorsJson(200, "application/json", response);
    }
}

void loadCalibration() {
    preferences.begin(CAL_NAMESPACE, true);
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

void handleCalibrate() {
    if (server.method() == HTTP_POST) {
        if (!server.hasArg("plain")) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Missing JSON body\"}");
            return;
        }

        String body = server.arg("plain");
        Serial.print("Received calibrate POST: ");
        Serial.println(body);

    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, body);

        if (error) {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.f_str());
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Invalid JSON\"}");
            return;
        }

        // Determine which sensor pin/index this calibration applies to
        int pinIndex = -1;
        if (doc["pin_index"].is<int>()) {
            pinIndex = doc["pin_index"].as<int>();
        } else if (doc["pin_number"].is<int>()) {
            int pinNumber = doc["pin_number"].as<int>();
            pinIndex = findVoltageSensorIndexByPin(pinNumber);
        }

        if (pinIndex < 0) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Invalid or missing pin_index/pin_number\"}");
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
        if (server.hasArg("pin_index")) {
            pinIndex = server.arg("pin_index").toInt();
        } else if (server.hasArg("pin_number")) {
            int pinNumber = server.arg("pin_number").toInt();
            pinIndex = findVoltageSensorIndexByPin(pinNumber);
        } else {
            // Default to first sensor if none provided
            pinIndex = 0;
        }

        if (pinIndex < 0) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Invalid pin index/number\"}");
            return;
        }

        struct SensorCalibration cal = getCalibrationForPin(pinIndex);
    DynamicJsonDocument doc(256);
        doc["pin_index"] = pinIndex;
            doc["pin_number"] = getVoltageSensorPin(pinIndex);
            doc["sensor_id"] = String("AI") + String(pinIndex + 1);
        doc["zero_raw_adc"] = cal.zeroRawAdc;
        doc["span_raw_adc"] = cal.spanRawAdc;
        doc["zero_pressure_value"] = cal.zeroPressureValue;
        doc["span_pressure_value"] = cal.spanPressureValue;
        doc["scale"] = cal.scale;
            sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");

        String response;
        serializeJson(doc, response);
        sendCorsJson(200, "application/json", response);
    }
}

// forward declaration for time endpoint handler
void handleTime();

void setupWebServer() {
    preferences.begin(PREF_NAMESPACE, false); // Initialize Preferences
    preferences.end();

    preferences.begin(CAL_NAMESPACE, false); // Initialize Calibration Preferences
    preferences.end();

    // Load config on startup
    loadConfig();

    server.on("/config", handleConfig);
    // register /time handler (defined below)
    server.on("/time", handleTime);
    // Root system info endpoint
    server.on("/", HTTP_GET, []() {
    DynamicJsonDocument doc(2048);
        // Network info
        doc["ip"] = WiFi.localIP().toString();
        doc["ap_ip"] = WiFi.softAPIP().toString();
        doc["mac"] = WiFi.macAddress();
        doc["ssid"] = WiFi.SSID();
        doc["rssi"] = WiFi.RSSI();
    doc["hostname"] = WiFi.getHostname();
    doc["wifi_hostname"] = WiFi.getHostname();
        doc["wifi_mode"] = (int)WiFi.getMode();

        // System / runtime
        doc["uptime_seconds"] = (unsigned long)(millis() / 1000);
        doc["uptime_human"] = String((unsigned long)(millis() / 1000 / 3600)) + "h" + String(((unsigned long)(millis() / 1000) % 3600) / 60) + "m";
    doc["free_heap"] = ESP.getFreeHeap();
    size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    doc["largest_free_block"] = (unsigned long)largest_block;
    int frag_pct = 0;
    if (ESP.getFreeHeap() > 0) frag_pct = 100 - (int)((largest_block * 100) / ESP.getFreeHeap());
    doc["heap_frag_percent"] = frag_pct;
        doc["cpu_freq_mhz"] = ESP.getCpuFreqMHz();

        // Flash / sketch info
        doc["sketch_size_bytes"] = (unsigned long)ESP.getSketchSize();
        doc["free_sketch_space_bytes"] = (unsigned long)ESP.getFreeSketchSpace();
        doc["flash_size_bytes"] = (unsigned long)ESP.getFlashChipSize();

        // Build info
        doc["build_date"] = __DATE__;
        doc["build_time"] = __TIME__;

        // Sensors
        doc["num_sensors_configured"] = getConfiguredNumSensors();
        doc["num_sensors_total"] = getNumVoltageSensors();

        // Time / RTC / NTP
        doc["rtc_present"] = isRtcPresent() ? 1 : 0;
        doc["rtc_enabled"] = getRtcEnabled() ? 1 : 0;
        doc["rtc_epoch"] = isRtcPresent() ? (unsigned long)getRtcEpoch() : 0;
        doc["last_ntp_epoch"] = (unsigned long)getLastNtpSuccessEpoch();
        doc["pending_rtc_sync"] = isPendingRtcSync() ? 1 : 0;

        // SD / Notifications
        doc["sd_enabled"] = getSdEnabled() ? 1 : 0;
        bool sd_mounted = false;
        if (SD.begin()) {
            sd_mounted = true;
        }
        doc["sd_mounted"] = sd_mounted ? 1 : 0;

        Preferences p;
        p.begin(PREF_NAMESPACE, true);
        int mode = p.getInt(PREF_NOTIFICATION_MODE, DEFAULT_NOTIFICATION_MODE);
        int payload = p.getInt(PREF_NOTIFICATION_PAYLOAD, DEFAULT_NOTIFICATION_PAYLOAD_TYPE);
        p.end();
        doc["notification_mode"] = mode;
        doc["notification_payload_type"] = payload;

        // OTA / MDNS
    doc["mdns_macro"] = MDNS_HOSTNAME;
    doc["ota_hostname"] = WiFi.getHostname();
        doc["ota_port"] = OTA_PORT;

        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });
    server.on("/time/sync", HTTP_POST, []() {
        // Trigger immediate NTP sync and request RTC update if RTC present
        syncNtp(isRtcPresent());
        sendCorsJson(200, "application/json", "{\"status\":\"ok\", \"message\":\"NTP sync triggered\"}");
    });
    server.on("/calibrate/pin", HTTP_POST, []() {
        if (!server.hasArg("plain")) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Missing JSON body\"}");
            return;
        }
        String body = server.arg("plain");
    JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);
        if (error) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Invalid JSON\"}");
            return;
        }

        if (!doc["pin_number"].is<int>()) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Missing pin_number\"}");
            return;
        }
        int pinNumber = doc["pin_number"].as<int>();
        int pinIndex = findVoltageSensorIndexByPin(pinNumber);
        if (pinIndex < 0) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Unknown pin_number\"}");
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
    server.on("/calibrate/default", HTTP_POST, []() {
        int n = getNumVoltageSensors();
        for (int i = 0; i < n; ++i) {
            saveCalibrationForPin(i, 0.0f, 4095.0f, 0.0f, 10.0f);
        }
        // Reseed smoothed ADCs so readings update immediately after calibration
        setupVoltagePressureSensor();
        sendCorsJson(200, "application/json", "{\"status\":\"success\", \"message\":\"Default calibration applied to all sensors\"}");
    });

    // Convenience: apply default calibration for a single pin by pin_number
    server.on("/calibrate/default/pin", HTTP_POST, []() {
        if (!server.hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing JSON body\"}"); return; }
        String body = server.arg("plain");
        DynamicJsonDocument doc(256);
        DeserializationError err = deserializeJson(doc, body);
        if (err) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}"); return; }
        if (!doc["pin_number"].is<int>()) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing pin_number\"}"); return; }
        int pinNumber = doc["pin_number"].as<int>();
        int pinIndex = findVoltageSensorIndexByPin(pinNumber);
        if (pinIndex < 0) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Unknown pin_number\"}"); return; }
        saveCalibrationForPin(pinIndex, 0.0f, 4095.0f, 0.0f, 10.0f);
        // Reseed smoothed ADC for immediate effect
        setupVoltagePressureSensor();
        sendCorsJson(200, "application/json", "{\"status\":\"success\", \"message\":\"Default calibration applied to pin\"}");
    });
    server.on("/calibrate", handleCalibrate);
    server.on("/calibrate/all", HTTP_GET, []() {
    DynamicJsonDocument doc(512);
        for (int i = 0; i < getNumVoltageSensors(); ++i) {
            struct SensorCalibration cal = getCalibrationForPin(i);
            JsonObject obj = doc[String(i)].to<JsonObject>();
            obj["pin_index"] = i;
                obj["pin_number"] = getVoltageSensorPin(i);
                obj["sensor_id"] = String("AI") + String(i + 1);
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
    server.on("/sensors/config", HTTP_GET, []() {
        int n = getConfiguredNumSensors();
    DynamicJsonDocument doc(1024);
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

    server.on("/sensors/config", HTTP_POST, []() {
        if (!server.hasArg("plain")) {
            sendCorsJson(400, "application/json", "{\"status\":\"error\", \"message\":\"Missing JSON body\"}");
            return;
        }
        String body = server.arg("plain");
    DynamicJsonDocument doc(256);
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
    server.on("/sensors/readings", HTTP_GET, []() {
        int n = getNumVoltageSensors();
        String fromUnit = "";
        if (server.hasArg("convert_from")) fromUnit = server.arg("convert_from");
    DynamicJsonDocument doc(2048);
        doc["num_sensors"] = n;
        JsonArray arr = doc["sensors"].to<JsonArray>();
        for (int i = 0; i < n; ++i) {
            JsonObject obj = arr.add<JsonObject>();
            int pin = getVoltageSensorPin(i);
            int raw = analogRead(pin);
            float smoothed = getSmoothedADC(i);
            float calibrated = getSmoothedVoltagePressure(i);
            // Determine effective raw to avoid single-sample 4095 spikes affecting reported pressure
            int raw_original = raw;
            int raw_effective = raw;
            bool saturated = isPinSaturated(i);
            if (raw == 4095 && !saturated) {
                // transient spike: use smoothed ADC (rounded) as effective raw for pressure calculations
                raw_effective = (int)round(smoothed);
            }
            // Volts from raw ADC and smoothed ADC
            float voltage_raw_v = convert010V(raw);
            float voltage_smoothed_v = convert010V(round(smoothed));
            float voltage_v = voltage_smoothed_v;
            // Compute pressure using calibration against RAW ADC units (calibration stored as pressure vs ADC)
            struct SensorCalibration cal = getCalibrationForPin(i);
            float pressure_from_raw = (raw_effective * cal.scale) + cal.offset;
            float pressure_from_smoothed = (round(smoothed) * cal.scale) + cal.offset;
            obj["sensor_index"] = i;
            obj["sensor_id"] = String("AI") + String(i+1);
            obj["sensor_pin"] = pin;
            // Include device chip identifier (compact efuse-based ID) for traceability
            obj["chip_id"] = getChipId();
            obj["enabled"] = getSensorEnabled(i) ? 1 : 0;
            obj["raw_adc"] = raw;
            obj["raw_adc_original"] = raw_original;
            obj["raw_adc_effective"] = raw_effective;
            obj["smoothed_adc"] = smoothed;
            obj["voltage_raw"] = voltage_raw_v;
            obj["voltage_smoothed"] = voltage_smoothed_v;
            obj["voltage"] = voltage_v; // volts (0-10V) - same as smoothed
            obj["pressure_from_raw"] = pressure_from_raw;
            obj["pressure_from_smoothed"] = pressure_from_smoothed;
            // Include calibration details to help debugging when pressure is zero
            obj["cal_zero_raw_adc"] = cal.zeroRawAdc;
            obj["cal_span_raw_adc"] = cal.spanRawAdc;
            obj["cal_zero_pressure_value"] = cal.zeroPressureValue;
            obj["cal_span_pressure_value"] = cal.spanPressureValue;
            obj["cal_scale"] = cal.scale;
            obj["cal_offset"] = cal.offset;
            // Report pressure simplified to bar only
            // `calibrated` is stored/represented as bar in this codebase
            obj["pressure_value"] = calibrated;
            obj["pressure_unit"] = "bar";
            obj["pressure_bar"] = calibrated;
            // Report if ADC pin appears saturated (consecutive full-scale reads)
            obj["saturated"] = isPinSaturated(i) ? 1 : 0;
        }
        // Also include ADS1115 raw reads for A0/A1 (water pressure current sensors)
        // These are raw, unfiltered readings (no smoothing) and reported in counts, mV and mA
        JsonArray adsArr = doc["ads_sensors"].to<JsonArray>();
        // Channels 0 and 1 correspond to A0 and A1 on the ADS1115
        for (int ch = 0; ch <= 1; ++ch) {
            JsonObject aobj = adsArr.add<JsonObject>();
            int16_t raw = readAdsRaw(ch);
            float mv = adsRawToMv(raw);
            // Use defaults from config.h if caller didn't override
            float ma = readAdsMa(ch, DEFAULT_SHUNT_OHM, DEFAULT_AMP_GAIN);
            float depth_mm = computeDepthMm(ma, DEFAULT_CURRENT_INIT_MA, DEFAULT_RANGE_MM, DEFAULT_DENSITY_WATER);
            aobj["ads_channel"] = ch;
            aobj["adc_chip"] = String("0x") + String((int)ADS1115_ADDR, HEX);
            aobj["raw"] = raw;
            aobj["mv"] = mv;
            aobj["ma"] = ma;
            aobj["depth_mm"] = depth_mm;
        }
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    // Notification config endpoints
    server.on("/notifications/config", HTTP_GET, []() {
    Preferences p;
    p.begin(PREF_NAMESPACE, true);
    int mode = p.getInt(PREF_NOTIFICATION_MODE, DEFAULT_NOTIFICATION_MODE);
    int payload = p.getInt(PREF_NOTIFICATION_PAYLOAD, DEFAULT_NOTIFICATION_PAYLOAD_TYPE);
    p.end();
    DynamicJsonDocument doc(128);
    doc["mode"] = mode;
    doc["payload_type"] = payload;
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    server.on("/notifications/config", HTTP_POST, []() {
    if (!server.hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing body\"}"); return; }
    String body = server.arg("plain");
    DynamicJsonDocument doc(128);
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
    server.on("/ads/config", HTTP_GET, []() {
        DynamicJsonDocument doc(512);
        JsonArray arr = doc["channels"].to<JsonArray>();
        for (int ch = 0; ch <= 1; ++ch) {
            JsonObject o = arr.add<JsonObject>();
            o["channel"] = ch;
            o["shunt_ohm"] = getAdsShuntOhm(ch);
            o["amp_gain"] = getAdsAmpGain(ch);
        }
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    server.on("/ads/config", HTTP_POST, []() {
        if (!server.hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing body\"}"); return; }
        String body = server.arg("plain");
        DynamicJsonDocument doc(512);
        DeserializationError err = deserializeJson(doc, body);
        if (err) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}"); return; }
        if (!doc["channels"].is<JsonArray>()) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing channels array\"}"); return; }
        Preferences p;
        p.begin("ads_cfg", false);
        for (JsonObject chObj : doc["channels"].as<JsonArray>()) {
            int ch = chObj["channel"].as<int>();
            if (ch < 0 || ch > 3) continue;
            if (chObj.containsKey("shunt_ohm")) {
                char key[16]; snprintf(key, sizeof(key), "shunt_%d", ch);
                p.putFloat(key, (float)chObj["shunt_ohm"].as<float>());
            }
            if (chObj.containsKey("amp_gain")) {
                char key[16]; snprintf(key, sizeof(key), "amp_%d", ch);
                p.putFloat(key, (float)chObj["amp_gain"].as<float>());
            }
        }
        p.end();
        sendCorsJson(200, "application/json", "{\"status\":\"success\",\"message\":\"ADS config saved\"}");
    });
    // Trigger notification(s) on demand: POST body may include { "sensor_index": int } or { "pin_number": int }
    server.on("/notifications/trigger", HTTP_POST, []() {
    if (!server.hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing body\"}"); return; }
        String body = server.arg("plain");
    DynamicJsonDocument doc(256);
        DeserializationError err = deserializeJson(doc, body);
    if (err) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}"); return; }

        // Determine target: specific sensor or all
        int targetIndex = -1;
        if (doc["sensor_index"].is<int>()) {
            targetIndex = doc["sensor_index"].as<int>();
                if (targetIndex < 0 || targetIndex >= getNumVoltageSensors()) {
                sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid sensor_index\"}");
                return;
            }
        } else if (doc["pin_number"].is<int>()) {
            int pin = doc["pin_number"].as<int>();
            targetIndex = findVoltageSensorIndexByPin(pin);
            if (targetIndex < 0) {
                sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Unknown pin_number\"}");
                return;
            }
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
    server.on("/time/config", HTTP_GET, []() {
        bool rtcEnabled = getRtcEnabled();
    DynamicJsonDocument doc(128);
        doc["rtc_enabled"] = rtcEnabled ? 1 : 0;
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    server.on("/time/config", HTTP_POST, []() {
    if (!server.hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing body\"}"); return; }
        String body = server.arg("plain");
    DynamicJsonDocument doc(128);
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
    server.on("/sd/config", HTTP_GET, []() {
    bool sd = getSdEnabled();
    DynamicJsonDocument doc(128);
    doc["sd_enabled"] = sd ? 1 : 0;
        String resp;
        serializeJson(doc, resp);
        sendCorsJson(200, "application/json", resp);
    });

    server.on("/sd/config", HTTP_POST, []() {
    if (!server.hasArg("plain")) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing body\"}"); return; }
        String body = server.arg("plain");
    DynamicJsonDocument doc(128);
    DeserializationError err = deserializeJson(doc, body);
    if (err) { sendCorsJson(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}"); return; }
        bool sd = doc["sd_enabled"].as<int>() != 0;
        setSdEnabled(sd);
        sendCorsJson(200, "application/json", "{\"status\":\"success\",\"message\":\"SD config updated\"}");
    });
    // Global OPTIONS handler for CORS preflight
    server.on("*", HTTP_OPTIONS, []() {
        setCorsHeaders();
        sendCorsJson(204, "text/plain", "");
    });
    server.begin();
    Serial.println("Web server started.");
}

// forward declaration
void handleTime();

void handleTime() {
    DynamicJsonDocument doc(256);
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
    server.handleClient();
}
