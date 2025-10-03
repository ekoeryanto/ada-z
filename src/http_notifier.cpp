#include "http_notifier.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <ArduinoJson.h>
#include <RTClib.h> // For DateTime object
#include "time_sync.h" // For extern declarations of rtc and rtcFound
#include "voltage_pressure_sensor.h"
#include "sensor_calibration_types.h"
#include "calibration_keys.h"
#include "device_id.h"
#include "storage_helpers.h"
#include "current_pressure_sensor.h"
#include "pins_config.h"
#include "sensors_config.h"
#include "sample_store.h"
#include "json_helper.h"
#include "modbus_manager.h"

unsigned long lastHttpNotificationMillis = 0;
static uint8_t notificationMode = DEFAULT_NOTIFICATION_MODE;
static uint8_t notificationPayloadType = DEFAULT_NOTIFICATION_PAYLOAD_TYPE;

// Ensure the system time is reasonably synced before generating notification timestamps.
// Returns true if a valid system time is available (either already valid or obtained via NTP within timeout).
static bool ensureTimeSynced(unsigned long timeoutMs = 3000) {
    time_t sys = time(nullptr);
    struct tm *tm_sys = localtime(&sys);
    bool sys_valid = (tm_sys && (tm_sys->tm_year + 1900) > 2016);
    if (sys_valid) return true;
    if (WiFi.status() != WL_CONNECTED) {
        #if ENABLE_VERBOSE_LOGS
        Serial.println("WiFi not connected, cannot perform NTP sync before notification.");
        #endif
        return false;
    }
    #if ENABLE_VERBOSE_LOGS
    Serial.println("Ensuring NTP sync before notification...");
    #endif
    syncNtp(true);
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        time_t now = time(nullptr);
        struct tm *tm_now = localtime(&now);
        if (tm_now && (tm_now->tm_year + 1900) > 2016) {
            #if ENABLE_VERBOSE_LOGS
            Serial.println("NTP sync obtained.");
            #endif
            return true;
        }
        delay(100);
    }
    #if ENABLE_VERBOSE_LOGS
    Serial.println("Timed out waiting for NTP sync.");
    #endif
    return false;
}

void setNotificationMode(uint8_t modeMask) {
    notificationMode = modeMask;
}

uint8_t getNotificationMode() {
    return notificationMode;
}

void setNotificationPayloadType(uint8_t payloadType) {
    notificationPayloadType = payloadType;
}

uint8_t getNotificationPayloadType() {
    return notificationPayloadType;
}

// Helper to post payload to HTTP webhook url if configured
static void postJsonToWebhook(const String &payload) {
    if (WiFi.status() != WL_CONNECTED) return;
    HTTPClient http;
    http.begin(HTTP_NOTIFICATION_URL);
#if USE_HTTP_NOTIFICATION_HEADERS
    for (int i = 0; i < NUM_HTTP_NOTIFICATION_HEADERS; i++) {
        http.addHeader(HTTP_NOTIFICATION_HEADERS[i].key, HTTP_NOTIFICATION_HEADERS[i].value);
    }
#endif
    http.addHeader("Content-Type", "application/json");
    #if ENABLE_VERBOSE_LOGS
    Serial.print("Posting webhook payload to ");
    Serial.println(HTTP_NOTIFICATION_URL);
    Serial.print("Payload: ");
    Serial.println(payload);
    #endif
    int code = http.POST(payload);
    #if ENABLE_VERBOSE_LOGS
    Serial.printf("HTTP Response code: %d\n", code);
    if (code > 0) {
        String resp = http.getString();
        Serial.println(resp);
    }
    #endif
    http.end();
}

// Route a single sensor notification (ADC) - simplified compact payload
void routeSensorNotification(int sensorIndex, int rawADC, float smoothedADC, float voltage) {
    // Keep timestamp and rtu; produce compact tags with id, source, enabled, value, unit
    JsonDocument doc;
    doc["timestamp"] = getIsoTimestamp();
    doc["rtu"] = String(getChipId());

    JsonArray tags = doc["tags"].to<JsonArray>();
    JsonObject s = tags.add<JsonObject>();
    s["id"] = String("AI") + String(sensorIndex + 1);
    s["source"] = "adc";
    s["enabled"] = getSensorEnabled(sensorIndex) ? 1 : 0;

    // Compute converted value (pressure in bar) and send only the converted value for compactness
    float avgRawF, avgSmoothedF, avgVoltF;
    int rawToUse = rawADC;
    float smoothedToUse = smoothedADC;
    if (getAverages(sensorIndex, avgRawF, avgSmoothedF, avgVoltF)) {
        rawToUse = (int)round(avgRawF);
        smoothedToUse = avgSmoothedF;
    }
    struct SensorCalibration cal = getCalibrationForPin(sensorIndex);
    float pressure_from_filtered = (round(smoothedToUse) * cal.scale) + cal.offset;
    s["value"] = roundToDecimals(pressure_from_filtered, 2);
    s["unit"] = "bar";

    String payload;
    serializeJson(doc, payload);

    if (notificationMode & NOTIF_MODE_SERIAL) {
        #if ENABLE_VERBOSE_LOGS
        Serial.print("Notification (serial): ");
        Serial.println(payload);
        #endif
    }
    if (notificationMode & NOTIF_MODE_WEBHOOK) {
        postJsonToWebhook(payload);
    }
}

// Send notification for an ADC sensor (public wrapper)
void sendHttpNotification(int sensorIndex, int rawADC, float smoothedADC, float voltage) {
    routeSensorNotification(sensorIndex, rawADC, smoothedADC, voltage);
}

// Send ADS notification - simplified compact payload
void sendAdsNotification(int adsChannel, int16_t rawAds, float mv, float ma) {
    JsonDocument doc;
    doc["timestamp"] = getIsoTimestamp();
    doc["rtu"] = String(getChipId());

    JsonArray tags = doc["tags"].to<JsonArray>();
    JsonObject a = tags.add<JsonObject>();
    a["id"] = String("ADS_A") + String(adsChannel);
    a["source"] = "ads1115";
    a["enabled"] = 1;

    float voltage_v = mv / 1000.0f;
    float pressure_bar = (voltage_v / 10.0f) * DEFAULT_RANGE_BAR;
    a["value"] = roundToDecimals(pressure_bar, 2);
    a["unit"] = "bar";

    String payload;
    serializeJson(doc, payload);

    if (notificationMode & NOTIF_MODE_SERIAL) {
        #if ENABLE_VERBOSE_LOGS
        Serial.print("Notification (serial): ");
        Serial.println(payload);
        #endif
    }
    if (notificationMode & NOTIF_MODE_WEBHOOK) {
        postJsonToWebhook(payload);
    }
}

// Send batch notification for multiple ADC sensors plus ADS channels appended
void sendHttpNotificationBatch(int numSensors, int sensorIndices[], int rawADC[], float smoothedADC[]) {
    // Compact batch payload: timestamp, rtu, tags[] { id, source, enabled, value, unit }
    JsonDocument doc;
    doc["timestamp"] = getIsoTimestamp();
    doc["rtu"] = String(getChipId());

    JsonArray arr = doc["tags"].to<JsonArray>();

    // ADC sensors -> value = converted pressure (bar)
    for (int i = 0; i < numSensors; ++i) {
        int sensorIndex = sensorIndices[i];
        JsonObject obj = arr.add<JsonObject>();
        obj["id"] = String("AI") + String(sensorIndex + 1);
        obj["source"] = "adc";
        obj["enabled"] = getSensorEnabled(sensorIndex) ? 1 : 0;

        int raw = rawADC[i];
        float smoothed = smoothedADC[i];
        struct SensorCalibration cal = getCalibrationForPin(sensorIndex);
        float pressure_from_raw = (round(raw) * cal.scale) + cal.offset;
        float pressure_from_smoothed = (round(smoothed) * cal.scale) + cal.offset;
        
        JsonObject val = obj.createNestedObject("value");
        val["raw"] = roundToDecimals(pressure_from_raw, 2);
        val["filtered"] = roundToDecimals(pressure_from_smoothed, 2);
        obj["unit"] = "bar";
    }

    // ADS channels -> value = derived pressure (bar)
    for (int ch = 0; ch <= 1; ++ch) {
        JsonObject a = arr.add<JsonObject>();
        a["id"] = String("ADS_A") + String(ch);
        a["source"] = "ads1115";
        a["enabled"] = 1;

        // Raw value
        int16_t raw = readAdsRaw(ch);
        float mv = adsRawToMv(raw);
        float voltage_v = mv / 1000.0f;
        float pressure_bar_raw = (voltage_v / 10.0f) * DEFAULT_RANGE_BAR;

        // Smoothed value
        float ma_smoothed = getAdsSmoothedMa(ch);
        float tp_scale = getAdsTpScale(ch);
        float mv_from_smoothed = ma_smoothed * tp_scale;
        float voltage_from_smoothed = mv_from_smoothed / 1000.0f;
        float pressure_bar_smoothed = (voltage_from_smoothed / 10.0f) * DEFAULT_RANGE_BAR;

        JsonObject val = a.createNestedObject("value");
        val["raw"] = roundToDecimals(pressure_bar_raw, 2);
        val["filtered"] = roundToDecimals(pressure_bar_smoothed, 2);
        a["unit"] = "bar";
    }

    doc["tags_total"] = arr.size();

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    if (notificationMode & NOTIF_MODE_SERIAL) {
        #if ENABLE_VERBOSE_LOGS
        Serial.print("Notification (serial): ");
        Serial.println(jsonPayload);
        #endif
    }
    if (notificationMode & NOTIF_MODE_WEBHOOK) {
        postJsonToWebhook(jsonPayload);
    }
}

