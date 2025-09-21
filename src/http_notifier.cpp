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

    JsonArray arr = doc["sensors"].to<JsonArray>();
    for (int i = 0; i < numSensors; ++i) {
        int sensorIndex = sensorIndices[i];
        JsonObject obj = arr.add<JsonObject>();
        obj["sensor_index"] = sensorIndex;
        obj["sensor_pin"] = getVoltageSensorPin(sensorIndex);
    obj["sensor_id"] = String("AI") + String(sensorIndex+1);
    // Include device chip identifier (compact efuse-based ID)
    obj["chip_id"] = getChipId();
        // Compute the same fields as /sensors/readings
        int raw = rawADC[i];
        float smoothed = smoothedADC[i];
        float voltage_raw_v = convert010V(raw);
        float voltage_smoothed_v = convert010V(round(smoothed));
        float pressure_from_raw = 0.0;
        float pressure_from_smoothed = 0.0;
        struct SensorCalibration cal = getCalibrationForPin(sensorIndex);
        // Apply ADC-based calibration (pressure = rawAdc * scale + offset)
        pressure_from_raw = (raw * cal.scale) + cal.offset;
        pressure_from_smoothed = (round(smoothed) * cal.scale) + cal.offset;
        float pressure_value = getSmoothedVoltagePressure(sensorIndex);

        obj["voltage_raw"] = voltage_raw_v;
        obj["voltage_smoothed"] = voltage_smoothed_v;
        obj["voltage"] = voltage_smoothed_v;
        obj["pressure_from_raw"] = pressure_from_raw;
        obj["pressure_from_smoothed"] = pressure_from_smoothed;
        obj["pressure_value"] = pressure_value;
        obj["pressure_unit"] = "bar";
        obj["pressure_bar"] = pressure_value;
        // Legacy fields
        obj["raw_adc"] = raw;
        obj["smoothed_adc"] = smoothed;
    }

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
    // Build simple JSON depending on payload type and route via serial/webhook
    DynamicJsonDocument doc(512);
    if (rtcFound) {
        DateTime now = rtc.now();
        char timestamp[25];
        sprintf(timestamp, "%04d-%02d-%02dT%02d:%02d:%02d",
                now.year(), now.month(), now.day(),
                now.hour(), now.minute(), now.second());
        doc["timestamp"] = timestamp;
    }
        JsonObject obj = doc["sensor"].to<JsonObject>();
        obj["sensor_index"] = sensorIndex;
        obj["sensor_pin"] = getVoltageSensorPin(sensorIndex);
    obj["sensor_id"] = String("AI") + String(sensorIndex+1);
    obj["chip_id"] = getChipId();
        obj["pressure_unit"] = "bar";
        obj["pressure_bar"] = voltage;
        // Keep legacy fields
        obj["rawADC"] = rawADC;
        obj["smoothedADC"] = smoothedADC;
        obj["voltage"] = voltage;
    
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
