#include "http_notifier.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
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

// Route a single sensor notification (ADC) according to current mode. Builds payload matching /api/sensors/readings
void routeSensorNotification(int sensorIndex, int rawADC, float smoothedADC, float voltage) {
    bool time_ok = ensureTimeSynced();
    StaticJsonDocument<1024> doc;
    doc["timestamp"] = getIsoTimestamp();
    doc["time_synced"] = time_ok ? 1 : 0;
    doc["timestamp_source"] = time_ok ? String("ntp/rtc/system") : String("unsynced");
    doc["rtu"] = String(getChipId());

    JsonArray tags = doc["tags"].to<JsonArray>();
    JsonObject s = tags.add<JsonObject>();
    s["id"] = String("AI") + String(sensorIndex + 1);
    s["port"] = getVoltageSensorPin(sensorIndex);
    s["index"] = sensorIndex;
    s["source"] = "adc";
    s["enabled"] = getSensorEnabled(sensorIndex) ? 1 : 0;

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
    val["raw"] = rawToUse;
    val["filtered"] = roundToDecimals(smoothedToUse, 3);

    JsonObject scaled = val["scaled"].to<JsonObject>();
    scaled["raw"] = roundToDecimals(convert010V(rawToUse), 3);
    scaled["filtered"] = roundToDecimals(convert010V((int)round(smoothedToUse)), 3);
    scaled["value"] = roundToDecimals(convert010V((int)round(smoothedToUse)), 2);
    scaled["unit"] = "volt";

    JsonObject conv = val["converted"].to<JsonObject>();
    struct SensorCalibration cal = getCalibrationForPin(sensorIndex);
    float pressure_from_raw = (rawToUse * cal.scale) + cal.offset;
    float pressure_from_filtered = (round(smoothedToUse) * cal.scale) + cal.offset;
    conv["value"] = roundToDecimals(pressure_from_filtered, 2);
    conv["unit"] = "bar";
    conv["semantic"] = "pressure";
    conv["raw"] = roundToDecimals(pressure_from_raw, 2);
    conv["filtered"] = roundToDecimals(pressure_from_filtered, 2);

    JsonObject meta = s["meta"].to<JsonObject>();
    meta["cal_zero_raw_adc"] = cal.zeroRawAdc;
    meta["cal_span_raw_adc"] = cal.spanRawAdc;
    meta["cal_zero_pressure_value"] = roundToDecimals(cal.zeroPressureValue, 2);
    meta["cal_span_pressure_value"] = roundToDecimals(cal.spanPressureValue, 2);
    meta["cal_scale"] = roundToDecimals(cal.scale, 4);
    meta["cal_offset"] = roundToDecimals(cal.offset, 4);

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

// Send ADS notification mirroring batch structure used in sendHttpNotificationBatch
void sendAdsNotification(int adsChannel, int16_t rawAds, float mv, float ma) {
    bool time_ok = ensureTimeSynced();
    StaticJsonDocument<1024> doc;
    doc["timestamp"] = getIsoTimestamp();
    doc["time_synced"] = time_ok ? 1 : 0;
    doc["timestamp_source"] = time_ok ? String("ntp/rtc/system") : String("unsynced");
    doc["rtu"] = String(getChipId());

    JsonArray tags = doc["tags"].to<JsonArray>();
    JsonObject a = tags.add<JsonObject>();
    a["id"] = String("ADS_A") + String(adsChannel);
    a["port"] = adsChannel;
    a["index"] = getNumVoltageSensors() + adsChannel;
    a["source"] = "ads1115";

    JsonObject val = a["value"].to<JsonObject>();
    val["raw"] = rawAds;
    float tp_scale = getAdsTpScale(adsChannel);
    float ma_smoothed = getAdsSmoothedMa(adsChannel);
    float mv_from_smoothed = ma_smoothed * tp_scale;
    float voltage_from_smoothed = mv_from_smoothed / 1000.0f;
    val["filtered"] = roundToDecimals(voltage_from_smoothed, 3);

    JsonObject scaled = val["scaled"].to<JsonObject>();
    scaled["raw"] = roundToDecimals(mv / 1000.0f, 3);
    scaled["filtered"] = roundToDecimals(voltage_from_smoothed, 3);
    scaled["value"] = roundToDecimals(mv / 1000.0f, 2);
    scaled["unit"] = "volt";

    JsonObject conv = val["converted"].to<JsonObject>();
    float voltage_v = mv / 1000.0f;
    float pressure_bar = (voltage_v / 10.0f) * DEFAULT_RANGE_BAR;
    conv["value"] = roundToDecimals(pressure_bar, 2);
    conv["unit"] = "bar";
    conv["semantic"] = "pressure";
    conv["note"] = "TP5551 derived";
    conv["raw"] = roundToDecimals(pressure_bar, 2);
    conv["filtered"] = roundToDecimals((voltage_from_smoothed / 10.0f) * DEFAULT_RANGE_BAR, 2);

    JsonObject meta = a["meta"].to<JsonObject>();
    JsonObject meta_meas = meta["measurement"].to<JsonObject>();
    meta_meas["mv"] = mv;
    meta_meas["ma"] = ma;
    meta["tp_model"] = String("TP5551");
    meta["tp_scale_mv_per_ma"] = tp_scale;
    meta["cal_tp_scale_mv_per_ma"] = tp_scale;
    meta["ma_smoothed"] = ma_smoothed;

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
    bool time_ok = ensureTimeSynced();
    StaticJsonDocument<4096> doc;
    doc["timestamp"] = getIsoTimestamp();
    doc["time_synced"] = time_ok ? 1 : 0;
    doc["timestamp_source"] = time_ok ? String("ntp/rtc/system") : String("unsynced");
    doc["rtu"] = String(getChipId());

    JsonArray arr = doc["tags"].to<JsonArray>();

    // ADC sensors
    for (int i = 0; i < numSensors; ++i) {
        int sensorIndex = sensorIndices[i];
        JsonObject obj = arr.add<JsonObject>();
        obj["id"] = String("AI") + String(sensorIndex + 1);
        obj["port"] = getVoltageSensorPin(sensorIndex);
        obj["index"] = sensorIndex;
        obj["source"] = "adc";
        obj["enabled"] = getSensorEnabled(sensorIndex) ? 1 : 0;

        int raw = rawADC[i];
        float smoothed = smoothedADC[i];
        float voltage_smoothed_v = convert010V((int)round(smoothed));
        struct SensorCalibration cal = getCalibrationForPin(sensorIndex);
        float pressure_from_raw = (raw * cal.scale) + cal.offset;
        float pressure_from_smoothed = (round(smoothed) * cal.scale) + cal.offset;
        float pressure_final = pressure_from_smoothed;

        JsonObject val = obj["value"].to<JsonObject>();
        val["raw"] = raw;
        val["filtered"] = smoothed;

        JsonObject scaled = val["scaled"].to<JsonObject>();
        scaled["raw"] = roundToDecimals(convert010V(raw), 3);
        scaled["filtered"] = roundToDecimals(voltage_smoothed_v, 3);
        scaled["value"] = roundToDecimals(voltage_smoothed_v, 2);
        scaled["unit"] = "volt";

        JsonObject conv = val["converted"].to<JsonObject>();
        conv["value"] = roundToDecimals(pressure_final, 2);
        conv["unit"] = "bar";
        conv["semantic"] = "pressure";
        conv["raw"] = roundToDecimals(pressure_from_raw, 2);
        conv["filtered"] = roundToDecimals(pressure_from_smoothed, 2);

        JsonObject audit = val["audit"].to<JsonObject>();
        float measured_voltage_v = voltage_smoothed_v;
        float expected_voltage_v = (pressure_final / DEFAULT_RANGE_BAR) * 10.0f;
        audit["measured_voltage_v"] = roundToDecimals(measured_voltage_v, 3);
        audit["expected_voltage_v_from_pressure"] = roundToDecimals(expected_voltage_v, 3);
        audit["voltage_delta_v"] = roundToDecimals(measured_voltage_v - expected_voltage_v, 3);

        JsonObject meta = obj["meta"].to<JsonObject>();
        meta["cal_zero_raw_adc"] = cal.zeroRawAdc;
        meta["cal_span_raw_adc"] = cal.spanRawAdc;
        meta["cal_zero_pressure_value"] = roundToDecimals(cal.zeroPressureValue, 2);
        meta["cal_span_pressure_value"] = roundToDecimals(cal.spanPressureValue, 2);
        meta["cal_scale"] = roundToDecimals(cal.scale, 4);
        meta["cal_offset"] = roundToDecimals(cal.offset, 4);
        meta["saturated"] = isPinSaturated(sensorIndex) ? 1 : 0;
    }

    // ADS channels appended to the same tags array
    for (int ch = 0; ch <= 1; ++ch) {
        JsonObject a = arr.add<JsonObject>();
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
        float voltage_v = mv / 1000.0f;
        float pressure_bar = (voltage_v / 10.0f) * DEFAULT_RANGE_BAR;

        JsonObject val = a["value"].to<JsonObject>();
        val["raw"] = raw;
        float ma_smoothed = getAdsSmoothedMa(ch);
        float mv_from_smoothed = ma_smoothed * tp_scale;
        float voltage_from_smoothed = mv_from_smoothed / 1000.0f;
        val["filtered"] = roundToDecimals(voltage_from_smoothed, 3);

        JsonObject scaled = val["scaled"].to<JsonObject>();
        scaled["raw"] = roundToDecimals(mv / 1000.0f, 3);
        scaled["filtered"] = roundToDecimals(voltage_from_smoothed, 3);
        scaled["value"] = roundToDecimals(mv / 1000.0f, 2);
        scaled["unit"] = "volt";

        JsonObject conv = val["converted"].to<JsonObject>();
        conv["value"] = roundToDecimals(pressure_bar, 2);
        conv["unit"] = "bar";
        conv["semantic"] = "pressure";
        conv["note"] = "derived from TP5551 using tp_scale_mv_per_ma";
        conv["raw"] = roundToDecimals(pressure_bar, 2);
        float pressure_from_smoothed = (voltage_from_smoothed / 10.0f) * DEFAULT_RANGE_BAR;
        conv["filtered"] = roundToDecimals(pressure_from_smoothed, 2);

        JsonObject meta = a["meta"].to<JsonObject>();
        JsonObject meta_meas = meta["measurement"].to<JsonObject>();
        meta_meas["mv"] = mv;
        meta_meas["ma"] = ma;
        meta["tp_model"] = String("TP5551");
        meta["tp_scale_mv_per_ma"] = tp_scale;
        meta["cal_tp_scale_mv_per_ma"] = tp_scale;
        meta["ma_smoothed"] = ma_smoothed;
        meta["depth_mm"] = depth_mm;
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

