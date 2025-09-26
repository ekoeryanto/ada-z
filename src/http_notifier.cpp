#include "http_notifier.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <ArduinoJson.h>
#pragma GCC diagnostic pop
#include <RTClib.h> // For DateTime object
#include "time_sync.h" // For extern declarations of rtc and rtcFound
#include "voltage_pressure_sensor.h"
#include "sensor_calibration_types.h"
#include "calibration_keys.h"
#include <Preferences.h>
#include "device_id.h"
#include "current_pressure_sensor.h"
#include "pins_config.h"
#include "sensors_config.h"
#include "sample_store.h"
#include "json_helper.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
// Using modern ArduinoJson APIs below (no deprecated createNestedX())
#pragma GCC diagnostic pop

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
        Serial.println("WiFi not connected, cannot perform NTP sync before notification.");
        return false;
    }
    Serial.println("Ensuring NTP sync before notification...");
    syncNtp(true);
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        time_t now = time(nullptr);
        struct tm *tm_now = localtime(&now);
        if (tm_now && (tm_now->tm_year + 1900) > 2016) {
            Serial.println("NTP sync obtained.");
            return true;
        }
        delay(100);
    }
    Serial.println("Timed out waiting for NTP sync.");
    return false;
}

void sendHttpNotification(int sensorIndex, int rawADC, float smoothedADC, float voltage) {
    // Route notification according to mode
    routeSensorNotification(sensorIndex, rawADC, smoothedADC, voltage);
}

// Send notification for an ADS channel (current sensor / TP5551). We build a small payload
// consistent with batch ADS entries and then route through the same webhook/serial paths.
void sendAdsNotification(int adsChannel, int16_t rawAds, float mv, float ma) {
    // Attempt to sync time but do not abort sending; include diagnostic fields
    bool time_ok = ensureTimeSynced();
    JsonDocDyn doc(512);
    doc["timestamp"] = getIsoTimestamp();
    doc["time_synced"] = time_ok ? 1 : 0;
    doc["timestamp_source"] = time_ok ? String("ntp/rtc/system") : String("unsynced");
    doc["rtu"] = String(getChipId());
    JsonArray tags = doc["tags"].to<JsonArray>();
    JsonObject a = tags.add<JsonObject>();
    a["id"] = String("ADS_A") + String(adsChannel);
    a["port"] = adsChannel;
    a["index"] = getNumVoltageSensors() + adsChannel; // follow same indexing as batch
    a["source"] = "ads1115";

    JsonObject val = a["value"].to<JsonObject>();
    JsonObject rawobj = val["raw"].to<JsonObject>();
    rawobj["value"] = rawAds;
    JsonObject conv = val["converted"].to<JsonObject>();
    float voltage_v = mv / 1000.0f;
    float pressure_bar = (voltage_v / 10.0f) * DEFAULT_RANGE_BAR;
    conv["value"] = roundToDecimals(pressure_bar, 2);
    conv["unit"] = "bar";
    conv["semantic"] = "pressure";
    conv["note"] = "TP5551 derived";
    conv["from_raw"] = roundToDecimals(pressure_bar, 2);
    float tp_scale = getAdsTpScale(adsChannel);
    float ma_smoothed = getAdsSmoothedMa(adsChannel);
    float mv_from_smoothed = ma_smoothed * tp_scale;
    float pressure_from_smoothed = (mv_from_smoothed / 1000.0f / 10.0f) * DEFAULT_RANGE_BAR;
    conv["from_filtered"] = roundToDecimals(pressure_from_smoothed, 2);

    JsonObject meta = a["meta"].to<JsonObject>();
    JsonObject meta_meas = meta["measurement"].to<JsonObject>();
    meta_meas["mv"] = roundToDecimals(mv, 2);
    meta_meas["ma"] = roundToDecimals(ma, 4);
    meta["tp_model"] = "TP5551";
    meta["tp_scale_mv_per_ma"] = roundToDecimals(tp_scale, 2);
    meta["cal_tp_scale_mv_per_ma"] = roundToDecimals(tp_scale, 2);

    String payload;
    serializeJson(doc, payload);

    if (notificationMode & NOTIF_MODE_SERIAL) {
        Serial.print("Notification (serial): ");
        Serial.println(payload);
    }
    if (notificationMode & NOTIF_MODE_WEBHOOK) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi not connected, skipping webhook notification.");
            return;
        }
        HTTPClient http;
        http.begin(HTTP_NOTIFICATION_URL);
        http.addHeader("Content-Type", "application/json");
        int code = http.POST(payload);
        if (code > 0) {
            Serial.printf("HTTP Response code: %d\n", code);
            String resp = http.getString();
            Serial.println(resp);
        } else {
            Serial.printf("HTTP Error: %s\n", http.errorToString(code).c_str());
        }
        http.end();
    }
}

void sendHttpNotificationBatch(int numSensors, int sensorIndices[], int rawADC[], float smoothedADC[]) {
    // Attempt to sync time but do not abort sending; include diagnostic fields
    bool time_ok = ensureTimeSynced();
    // Build payload according to configured payload type
    JsonDocDyn doc(1024);
    doc["timestamp"] = getIsoTimestamp();
    doc["time_synced"] = time_ok ? 1 : 0;
    doc["timestamp_source"] = time_ok ? String("ntp/rtc/system") : String("unsynced");

    // Top-level RTU and flat `tags` array
    doc["rtu"] = String(getChipId());
    JsonArray arr = doc["tags"].to<JsonArray>();
    for (int i = 0; i < numSensors; ++i) {
        int sensorIndex = sensorIndices[i];
    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = String("AI") + String(sensorIndex + 1);
    obj["port"] = getVoltageSensorPin(sensorIndex);
    obj["index"] = sensorIndex;
    obj["source"] = "adc";
    obj["enabled"] = getSensorEnabled(sensorIndex) ? 1 : 0;

        // Compute same metric as /sensors/readings
        int raw = rawADC[i];
        float smoothed = smoothedADC[i];
        float voltage_smoothed_v = convert010V(round(smoothed));
        struct SensorCalibration cal = getCalibrationForPin(sensorIndex);
    float pressure_from_raw = (raw * cal.scale) + cal.offset;
    float pressure_from_filtered = (round(smoothed) * cal.scale) + cal.offset;
    // Use the locally computed filtered->pressure value here to ensure the
    // "value" field in the notification is consistent with the
    // "from_filtered"/"from_raw" fields. Previously this called
    // getSmoothedVoltagePressure() which reads the module-global
    // smoothedADC[] and could be out-of-sync with the smoothed value
    // passed into this function, producing inconsistent output.
    float converted = pressure_from_filtered;

        JsonObject val = obj["value"].to<JsonObject>();
        JsonObject rawObj = val["raw"].to<JsonObject>();
        rawObj["value"] = raw;
        rawObj["original"] = raw;
        rawObj["effective"] = raw;
        // Round filtered value to 2 decimals
        val["filtered"] = roundToDecimals(smoothed, 2);
    // ADC: omit measurement object; keep scaled/converted only
            JsonObject scaled = val["scaled"].to<JsonObject>();
            scaled["value"] = roundToDecimals(voltage_smoothed_v, 2);
            scaled["unit"] = "volt";
    JsonObject conv = val["converted"].to<JsonObject>();
    conv["value"] = roundToDecimals(converted, 2);
    conv["unit"] = "bar";
    conv["semantic"] = "pressure";
    conv["from_raw"] = roundToDecimals(pressure_from_raw, 2);
    conv["from_filtered"] = roundToDecimals(pressure_from_filtered, 2);
    // Explicitly include smoothed/filtered converted value with fixed decimals
    conv["from_filtered_value"] = roundToDecimals(pressure_from_filtered, 2);
    conv["from_filtered_unit"] = String("bar");
    }

    // Include ADS1115 A0/A1 channel readings in the same `tags` array
    JsonArray adsArr = doc["tags"].as<JsonArray>();
    for (int ch = 0; ch <= 1; ++ch) {
    JsonObject a = adsArr.add<JsonObject>();
    int idx = numSensors + ch;
    a["id"] = String("ADS_A") + String(ch);
    a["port"] = ch;
    a["index"] = idx;
    a["source"] = "ads1115";
    a["adc_chip"] = String("0x") + String((int)ADS1115_ADDR, HEX);

        int16_t raw = readAdsRaw(ch);
        float mv = adsRawToMv(raw);
        float tp_scale = getAdsTpScale(ch);
        float ma = readAdsMa(ch, DEFAULT_SHUNT_OHM, DEFAULT_AMP_GAIN);
        float depth_mm = computeDepthMm(ma, DEFAULT_CURRENT_INIT_MA, DEFAULT_RANGE_MM, DEFAULT_DENSITY_WATER);
    // Map scaled voltage directly to pressure assuming sensor outputs 0-10V -> 0-DEFAULT_RANGE_BAR
    float voltage_v = mv / 1000.0f;
    float pressure_bar = (voltage_v / 10.0f) * DEFAULT_RANGE_BAR;

    JsonObject val = a["value"].to<JsonObject>();
    JsonObject rawA = val["raw"].to<JsonObject>();
    rawA["value"] = raw;
    JsonObject conv = val["converted"].to<JsonObject>();
    conv["value"] = roundToDecimals(pressure_bar, 2);
    conv["unit"] = "bar";
    conv["semantic"] = "pressure";
    conv["note"] = "derived from TP5551 using tp_scale_mv_per_ma";
    // Provide from_raw and from_filtered (smoothed) converted pressure for ADS
    conv["from_raw"] = roundToDecimals(pressure_bar, 2);
    float ma_smoothed = getAdsSmoothedMa(ch);
    float mv_from_smoothed = ma_smoothed * tp_scale;
    float voltage_from_smoothed = mv_from_smoothed / 1000.0f;
    float pressure_from_smoothed = (voltage_from_smoothed / 10.0f) * DEFAULT_RANGE_BAR;
    conv["from_filtered"] = roundToDecimals(pressure_from_smoothed, 2);
    JsonObject meta = a["meta"].to<JsonObject>();
    JsonObject meta_meas = meta["measurement"].to<JsonObject>();
        meta_meas["mv"] = roundToDecimals(mv, 2);
        meta_meas["ma"] = roundToDecimals(ma, 2);
        meta["tp_model"] = "TP5551";
    meta["tp_scale_mv_per_ma"] = roundToDecimals(tp_scale, 2);
    // Expose calibration key for tp_scale under cal_ prefix for consistency
    meta["cal_tp_scale_mv_per_ma"] = roundToDecimals(tp_scale, 2);
        meta["ma_smoothed"] = getAdsSmoothedMa(ch);
    }
#
    // Now that ADS entries are appended, serialize the final payload
    String jsonPayload;
    serializeJson(doc, jsonPayload);

    // Route according to mode: serial and/or webhook
    if (notificationMode & NOTIF_MODE_SERIAL) {
        Serial.print("Notification (serial): ");
        Serial.println(jsonPayload);
    }
    if (notificationMode & NOTIF_MODE_WEBHOOK) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi not connected, skipping webhook notification.");
        } else {
            HTTPClient http;
            http.begin(HTTP_NOTIFICATION_URL);
#if USE_HTTP_NOTIFICATION_HEADERS
            for (int i = 0; i < NUM_HTTP_NOTIFICATION_HEADERS; i++) {
                http.addHeader(HTTP_NOTIFICATION_HEADERS[i].key, HTTP_NOTIFICATION_HEADERS[i].value);
            }
#endif
            http.addHeader("Content-Type", "application/json");
            Serial.print("Sending HTTP batch POST to ");
            Serial.print(HTTP_NOTIFICATION_URL);
            Serial.print(" with payload: ");
            Serial.println(jsonPayload);
            int httpResponseCode = http.POST(jsonPayload);
            if (httpResponseCode > 0) {
                Serial.printf("HTTP Response code: %d\n", httpResponseCode);
                String responsePayload = http.getString();
                Serial.println(responsePayload);
            } else {
                Serial.printf("HTTP Error: %s\n", http.errorToString(httpResponseCode).c_str());
            }
            http.end();
        }
    }
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

void routeSensorNotification(int sensorIndex, int rawADC, float smoothedADC, float voltage) {
    // Attempt to sync time but do not abort sending; include diagnostic fields
    bool time_ok = ensureTimeSynced();
    // Build RTU-grouped `tags` payload for a single sensor
    JsonDocDyn doc(1024);
    doc["timestamp"] = getIsoTimestamp();
    doc["time_synced"] = time_ok ? 1 : 0;
    doc["timestamp_source"] = time_ok ? String("ntp/rtc/system") : String("unsynced");

    // Top-level `rtu` and flat `tags` array for single-sensor notification
    doc["rtu"] = String(getChipId());
    JsonArray tags = doc["tags"].to<JsonArray>();
    JsonObject s = tags.add<JsonObject>();
    s["id"] = String("AI") + String(sensorIndex + 1);
    s["port"] = getVoltageSensorPin(sensorIndex);
    s["index"] = sensorIndex;
    s["source"] = "adc";
    s["enabled"] = getSensorEnabled(sensorIndex) ? 1 : 0;

    // Prefer averages from sample store
    float avgRawF, avgSmoothedF, avgVoltF;
    int rawToUse = rawADC;
    float smoothedToUse = smoothedADC;
    float voltToUse = voltage;
    if (getAverages(sensorIndex, avgRawF, avgSmoothedF, avgVoltF)) {
        rawToUse = (int)round(avgRawF);
        smoothedToUse = avgSmoothedF;
        voltToUse = avgVoltF;
    }

    JsonObject val = s["value"].to<JsonObject>();
    JsonObject rawObj = val["raw"].to<JsonObject>();
    rawObj["value"] = rawToUse;
    rawObj["original"] = rawADC;
    rawObj["effective"] = rawToUse;
    val["filtered"] = roundToDecimals(smoothedToUse, 2);
    // ADC: omit measurement object; keep scaled/converted only
    JsonObject scaled = val["scaled"].to<JsonObject>();
    scaled["value"] = roundToDecimals(convert010V((int)round(smoothedADC)), 2);
    scaled["unit"] = "volt";
    JsonObject conv = val["converted"].to<JsonObject>();
    conv["value"] = roundToDecimals(voltage, 2); // interpreted as pressure (bar)
    conv["unit"] = "bar";
    struct SensorCalibration cal = getCalibrationForPin(sensorIndex);
    float pressure_from_raw = (rawADC * cal.scale) + cal.offset;
    float pressure_from_filtered = (round(smoothedADC) * cal.scale) + cal.offset;
    conv["semantic"] = "pressure";
    conv["from_raw"] = roundToDecimals(pressure_from_raw, 2);
    conv["from_filtered"] = roundToDecimals(pressure_from_filtered, 2);
    // Also expose the smoothed converted value for consumers
    conv["from_filtered_value"] = roundToDecimals(pressure_from_filtered, 2);
    conv["from_filtered_unit"] = String("bar");

    JsonObject meta = s["meta"].to<JsonObject>();
    meta["cal_zero_raw_adc"] = cal.zeroRawAdc;
    meta["cal_span_raw_adc"] = cal.spanRawAdc;
    meta["cal_zero_pressure_value"] = roundToDecimals(cal.zeroPressureValue, 2);
    meta["cal_span_pressure_value"] = roundToDecimals(cal.spanPressureValue, 2);
    meta["cal_scale"] = roundToDecimals(cal.scale, 4); // keep scale with more precision
    meta["cal_offset"] = roundToDecimals(cal.offset, 4);

    String payload;
    serializeJson(doc, payload);

    if (notificationMode & NOTIF_MODE_SERIAL) {
        Serial.print("Notification (serial): ");
        Serial.println(payload);
    }
    if (notificationMode & NOTIF_MODE_WEBHOOK) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi not connected, skipping webhook notification.");
            return;
        }
        HTTPClient http;
        http.begin(HTTP_NOTIFICATION_URL);
#if USE_HTTP_NOTIFICATION_HEADERS
        for (int i = 0; i < NUM_HTTP_NOTIFICATION_HEADERS; i++) {
            http.addHeader(HTTP_NOTIFICATION_HEADERS[i].key, HTTP_NOTIFICATION_HEADERS[i].value);
        }
#endif
        http.addHeader("Content-Type", "application/json");
        Serial.print("Sending HTTP POST to ");
        Serial.print(HTTP_NOTIFICATION_URL);
        Serial.print(" with payload: ");
        Serial.println(payload);
        int httpResponseCode = http.POST(payload);
        if (httpResponseCode > 0) {
            Serial.printf("HTTP Response code: %d\n", httpResponseCode);
            String responsePayload = http.getString();
            Serial.println(responsePayload);
        } else {
            Serial.printf("HTTP Error: %s\n", http.errorToString(httpResponseCode).c_str());
        }
        http.end();
    }
}
