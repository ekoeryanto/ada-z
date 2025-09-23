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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
// Using modern ArduinoJson APIs below (no deprecated createNestedX())
#pragma GCC diagnostic pop

unsigned long lastHttpNotificationMillis = 0;
static uint8_t notificationMode = DEFAULT_NOTIFICATION_MODE;
static uint8_t notificationPayloadType = DEFAULT_NOTIFICATION_PAYLOAD_TYPE;

void sendHttpNotification(int sensorIndex, int rawADC, float smoothedADC, float voltage) {
    // Route notification according to mode
    routeSensorNotification(sensorIndex, rawADC, smoothedADC, voltage);
}

void sendHttpNotificationBatch(int numSensors, int sensorIndices[], int rawADC[], float smoothedADC[]) {
    // Build payload according to configured payload type
    DynamicJsonDocument doc(2048);
    if (rtcFound) {
        DateTime now = rtc.now();
        char timestamp[25];
        sprintf(timestamp, "%04d-%02d-%02dT%02d:%02d:%02d",
                now.year(), now.month(), now.day(),
                now.hour(), now.minute(), now.second());
        doc["timestamp"] = timestamp;
    }

    // Top-level RTU and flat `tags` array
    doc["rtu"] = String(getChipId());
    JsonArray arr = doc.createNestedArray("tags");
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
        float converted = getSmoothedVoltagePressure(sensorIndex);

        JsonObject val = obj.createNestedObject("value");
        JsonObject rawObj = val.createNestedObject("raw");
        rawObj["value"] = raw;
        rawObj["original"] = raw;
        rawObj["effective"] = raw;
        val["filtered"] = smoothed;
    // ADC: omit measurement object; keep scaled/converted only
            JsonObject scaled = val.createNestedObject("scaled");
            scaled["value"] = voltage_smoothed_v;
            scaled["unit"] = "volt";
    JsonObject conv = val.createNestedObject("converted");
    conv["value"] = converted;
    conv["unit"] = "bar";
    conv["semantic"] = "pressure";
    conv["from_raw"] = pressure_from_raw;
    conv["from_filtered"] = pressure_from_filtered;
    }

    String jsonPayload;
    serializeJson(doc, jsonPayload);

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

        JsonObject val = a.createNestedObject("value");
        JsonObject rawA = val.createNestedObject("raw");
        rawA["value"] = raw;
    JsonObject conv = val.createNestedObject("converted");
    conv["value"] = pressure_bar;
    conv["unit"] = "bar";
    conv["semantic"] = "pressure";
    conv["note"] = "derived from TP5551 using tp_scale_mv_per_ma";
        JsonObject meta = a.createNestedObject("meta");
        JsonObject meta_meas = meta.createNestedObject("measurement");
        meta_meas["mv"] = mv;
        meta_meas["ma"] = ma;
        meta["tp_model"] = "TP5551";
        meta["tp_scale_mv_per_ma"] = tp_scale;
        meta["ma_smoothed"] = getAdsSmoothedMa(ch);
    }

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
    // Build RTU-grouped `tags` payload for a single sensor
    DynamicJsonDocument doc(1024);
    if (rtcFound) {
        DateTime now = rtc.now();
        char timestamp[25];
        sprintf(timestamp, "%04d-%02d-%02dT%02d:%02d:%02d",
                now.year(), now.month(), now.day(),
                now.hour(), now.minute(), now.second());
        doc["timestamp"] = timestamp;
    }

    // Top-level `rtu` and flat `tags` array for single-sensor notification
    doc["rtu"] = String(getChipId());
    JsonArray tags = doc.createNestedArray("tags");
    JsonObject s = tags.add<JsonObject>();
    s["id"] = String("AI") + String(sensorIndex + 1);
    s["port"] = getVoltageSensorPin(sensorIndex);
    s["index"] = sensorIndex;
    s["source"] = "adc";
    s["enabled"] = getSensorEnabled(sensorIndex) ? 1 : 0;

    JsonObject val = s.createNestedObject("value");
    JsonObject rawObj = val.createNestedObject("raw");
    rawObj["value"] = rawADC;
    rawObj["original"] = rawADC;
    rawObj["effective"] = rawADC;
    val["filtered"] = smoothedADC;
    // ADC: omit measurement object; keep scaled/converted only
    JsonObject scaled = val.createNestedObject("scaled");
    scaled["value"] = convert010V((int)round(smoothedADC));
    scaled["unit"] = "volt";
    JsonObject conv = val.createNestedObject("converted");
    conv["value"] = voltage; // interpreted as pressure (bar)
    conv["unit"] = "bar";
    struct SensorCalibration cal = getCalibrationForPin(sensorIndex);
    float pressure_from_raw = (rawADC * cal.scale) + cal.offset;
    float pressure_from_filtered = (round(smoothedADC) * cal.scale) + cal.offset;
    conv["semantic"] = "pressure";
    conv["from_raw"] = pressure_from_raw;
    conv["from_filtered"] = pressure_from_filtered;

    JsonObject meta = s.createNestedObject("meta");
    meta["cal_zero_raw_adc"] = cal.zeroRawAdc;
    meta["cal_span_raw_adc"] = cal.spanRawAdc;
    meta["cal_zero_pressure_value"] = cal.zeroPressureValue;
    meta["cal_span_pressure_value"] = cal.spanPressureValue;
    meta["cal_scale"] = cal.scale;
    meta["cal_offset"] = cal.offset;

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
