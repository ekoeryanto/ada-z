// Sensor and calibration related HTTP handlers
#include "web_api_common.h"
#include "web_api_json.h"
#include "web_api_handlers.h"

#include "sensors_config.h"
#include "voltage_pressure_sensor.h"
#include "calibration_keys.h"
#include "config.h"
#include "sensor_calibration_types.h"
#include "sample_store.h"
#include "storage_helpers.h"
#include "json_helper.h"
#include "current_pressure_sensor.h"
#include "modbus_manager.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <math.h>
#include <cstring>

// Need AsyncEventSource for SSE
#include <AsyncEventSource.h>

// Register the handlers previously defined inline in web_api.cpp
void registerSensorHandlers(AsyncWebServer *server) {
    if (!server) return;

    // Create the debug SSE event source if not already created.
    if (!eventSourceDebug) {
        eventSourceDebug = new AsyncEventSource("/sse/debug_sensors");
        server->addHandler(eventSourceDebug);
    }
    // Also register alias under /api so the Vite dev server proxy can forward it
    if (!eventSourceDebugAlias) {
        eventSourceDebugAlias = new AsyncEventSource("/api/sse/stream");
        server->addHandler(eventSourceDebugAlias);
    }

    ensureSensorSseRegistered(server);

    // Expose sensor/tag endpoints
    auto handleTagRead = [](AsyncWebServerRequest *request) {
        int sampling = 0;
        if (request->hasParam("sampling")) sampling = request->getParam("sampling")->value().toInt();
        if (sampling < 0) sampling = 0;

        String tag = request->hasParam("tag") ? request->getParam("tag")->value() : "";
        if (tag.length() == 0) {
            auto resp = makeErrorDoc("Missing tag");
            sendCorsJsonDoc(request, 400, resp);
            return;
        }

        int pinIndex = tagToIndex(tag);
        if (pinIndex < 0) {
            auto resp = makeErrorDoc("Unknown tag");
            sendCorsJsonDoc(request, 400, resp);
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
            avgRaw = (float)analogRead(pin);
            avgSmoothed = getSmoothedADC(pinIndex);
            if (avgSmoothed <= 0.0f) avgSmoothed = avgRaw;
            avgVolt = getSmoothedVoltagePressure(pinIndex);
            samplesUsed = 1;
        }

        struct SensorCalibration cal = getCalibrationForPin(pinIndex);
        float converted = haveAvg ? avgVolt : (avgSmoothed * cal.scale) + cal.offset;

        JsonDocument outDocStream;
        outDocStream["tag"] = tag;
        outDocStream["pin_index"] = pinIndex;
        outDocStream["pin"] = pin;
        outDocStream["samples_requested"] = sampling;
        outDocStream["samples_used"] = samplesUsed;
        outDocStream["measured_raw_avg"] = roundToDecimals(avgRaw, 2);
        outDocStream["measured_filtered_avg"] = roundToDecimals(avgSmoothed, 2);
        outDocStream["converted"]["value"] = roundToDecimals(converted, 2);
        outDocStream["converted"]["unit"] = "bar";
        sendCorsJsonDoc(request, 200, outDocStream);
    };

    server->on("/api/tag", HTTP_GET, handleTagRead);

    // Sensors config endpoints (GET and POST)
    server->on("/api/sensors/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        int n = getConfiguredNumSensors();
        JsonDocument doc;
        doc["num_sensors"] = n;
        JsonArray arr = doc["sensors"].to<JsonArray>();
        for (int i = 0; i < n; ++i) {
            JsonObject obj = arr.add<JsonObject>();
            obj["sensor_index"] = i;
            obj["sensor_pin"] = getVoltageSensorPin(i);
            obj["enabled"] = getSensorEnabled(i) ? 1 : 0;
            obj["notification_interval_ms"] = getSensorNotificationInterval(i);
        }
        sendCorsJsonDoc(request, 200, doc);
    });

    AsyncCallbackJsonWebHandler* sensorsConfigHandler = new AsyncCallbackJsonWebHandler("/api/sensors/config", [](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject doc = json.as<JsonObject>();
        if (doc.isNull()) {
            auto resp = makeErrorDoc("Invalid JSON");
            sendCorsJsonDoc(request, 400, resp);
            return;
        }

        if (!doc["sensors"].is<JsonArray>()) {
            auto resp = makeErrorDoc("Missing sensors array", 160);
            sendCorsJsonDoc(request, 400, resp);
            return;
        }

        JsonArray arr = doc["sensors"].as<JsonArray>();
        for (JsonObject sensor : arr) {
            int idx = sensor["sensor_index"].as<int>();
            bool enabled = sensor["enabled"].is<bool>() ? sensor["enabled"].as<bool>() : getSensorEnabled(idx);
            unsigned long interval = sensor["notification_interval_ms"].is<unsigned long>() ? sensor["notification_interval_ms"].as<unsigned long>() : getSensorNotificationInterval(idx);
            setSensorEnabled(idx, enabled);
            setSensorNotificationInterval(idx, interval);
            String enKey = String(PREF_SENSOR_ENABLED_PREFIX) + String(idx);
            String ivKey = String(PREF_SENSOR_INTERVAL_PREFIX) + String(idx);
            saveIntToNVSns("sensors", enKey.c_str(), enabled ? 1 : 0);
            saveULongToNVSns("sensors", ivKey.c_str(), interval);
        }
        auto resp = makeSuccessDoc("Sensor config updated");
        sendCorsJsonDoc(request, 200, resp);
    });
    sensorsConfigHandler->setMaxContentLength(2048);
    server->addHandler(sensorsConfigHandler);

    // Live sensor readings
    server->on("/api/sensors/readings", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc; // size chosen to accommodate sensors payload
        buildSensorsReadingsJson(doc);
        sendCorsJsonDoc(request, 200, doc);
    });

    // Calibration endpoints
    server->on("/api/calibrate", HTTP_GET, [](AsyncWebServerRequest *request) {
        // reuse centralized build
        JsonDocument doc;
        // Provide a list or single sensor info as appropriate
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
        sendCorsJsonDoc(request, 200, doc);
    });

    // cal pin handlers (POST) and other calibration-related handlers are left to be
    // registered where the platform expects JSON bodies (similar to original file)
    AsyncCallbackJsonWebHandler* calPinHandler = new AsyncCallbackJsonWebHandler("/api/calibrate/pin", [](AsyncWebServerRequest *request, JsonVariant &json) {
        // Delegate to existing logic in web_api.cpp (keeps behavior identical)
        JsonObject doc = json.as<JsonObject>();
        if (doc.isNull()) {
            auto resp = makeErrorDoc("Invalid JSON");
            sendCorsJsonDoc(request, 400, resp);
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
            auto resp = makeErrorDoc("Invalid or missing pin_index/pin");
            sendCorsJsonDoc(request, 400, resp);
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
            auto resp = makeSuccessDoc("Calibration points saved");
            sendCorsJsonDoc(request, 200, resp);
            return;
        }

        // Trigger zero calibration
        if (doc["trigger_zero_calibration"].is<bool>() && doc["trigger_zero_calibration"].as<bool>()) {
            float currentRawAdc = getSmoothedADC(pinIndex);
            struct SensorCalibration cal = getCalibrationForPin(pinIndex);
            saveCalibrationForPin(pinIndex, currentRawAdc, cal.spanRawAdc, 0.0f, cal.spanPressureValue);
            auto resp = makeSuccessDoc("Zero calibration set");
            sendCorsJsonDoc(request, 200, resp);
            return;
        }

        // Trigger span calibration
        if (doc["trigger_span_calibration"].is<bool>() && doc["trigger_span_calibration"].as<bool>() && doc["span_pressure_value"].is<float>()) {
            float currentRawAdc = getSmoothedADC(pinIndex);
            struct SensorCalibration cal = getCalibrationForPin(pinIndex);
            saveCalibrationForPin(pinIndex, cal.zeroRawAdc, currentRawAdc, cal.zeroPressureValue, doc["span_pressure_value"].as<float>());
            auto resp = makeSuccessDoc("Span calibration set");
            sendCorsJsonDoc(request, 200, resp);
            return;
        }

        auto resp = makeErrorDoc("Invalid calibration parameters");
        sendCorsJsonDoc(request, 400, resp);
    });
    calPinHandler->setMaxContentLength(1024);
    server->addHandler(calPinHandler);

    // Default calibration endpoints
    server->on("/api/calibrate/default", HTTP_POST, [](AsyncWebServerRequest *request) {
        int n = getNumVoltageSensors();
        for (int i = 0; i < n; ++i) {
            saveCalibrationForPin(i, 0.0f, 4095.0f, 0.0f, 10.0f);
        }
        setupVoltagePressureSensor();
        auto resp = makeSuccessDoc("Default calibration applied to all sensors", 160);
        sendCorsJsonDoc(request, 200, resp);
    });

    AsyncCallbackJsonWebHandler* calDefPinHandler = new AsyncCallbackJsonWebHandler("/api/calibrate/default/pin", [](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject doc = json.as<JsonObject>();
        if (doc.isNull()) {
            auto resp = makeErrorDoc("Invalid JSON");
            sendCorsJsonDoc(request, 400, resp);
            return;
        }
        int pinIndex = -1;
        if (!doc["pin"].isNull()) {
            int pinNumber = doc["pin"].as<int>();
            pinIndex = findVoltageSensorIndexByPin(pinNumber);
        } else if (!doc["tag"].isNull()) {
            pinIndex = tagToIndex(doc["tag"].as<String>());
        } else {
            auto resp = makeErrorDoc("Missing pin or tag");
            sendCorsJsonDoc(request, 400, resp);
            return;
        }
        if (pinIndex < 0) {
            auto resp = makeErrorDoc("Unknown sensor/pin");
            sendCorsJsonDoc(request, 400, resp);
            return;
        }
        saveCalibrationForPin(pinIndex, 0.0f, 4095.0f, 0.0f, 10.0f);
        setupVoltagePressureSensor();
        auto resp = makeSuccessDoc("Default calibration applied to pin", 160);
        sendCorsJsonDoc(request, 200, resp);
    });
    calDefPinHandler->setMaxContentLength(256);
    server->addHandler(calDefPinHandler);

    // ADC config & handlers
    server->on("/api/adc/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["adc_num_samples"] = getAdcNumSamples();
        doc["samples_per_sensor"] = getSampleCapacity();
        float scale = 1.0f;
        float offset = 0.0f;
        getVoltageLinearCalibration(scale, offset);
        doc["linear_scale"] = scale;
        doc["linear_offset"] = offset;
        JsonArray dividerArr = doc["divider_scale"].to<JsonArray>();
        const float *dividers = getAllAdcDividerScales();
        int num = getNumVoltageSensors();
        for (int i = 0; i < num; ++i) {
            dividerArr.add(dividers[i]);
        }
        sendCorsJsonDoc(request, 200, doc);
    });

    AsyncCallbackJsonWebHandler* adcConfigHandler = new AsyncCallbackJsonWebHandler("/api/adc/config", [](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject doc = json.as<JsonObject>();
        if (doc.isNull()) {
            auto resp = makeErrorDoc("Invalid JSON");
            sendCorsJsonDoc(request, 400, resp);
            return;
        }
        bool changed = false;
        if (!doc["adc_num_samples"].isNull()) {
            int ns = doc["adc_num_samples"].as<int>();
            setAdcNumSamples(ns);
            saveIntToNVSns("adc_cfg", "num_samples", ns);
            changed = true;
        }
        if (!doc["samples_per_sensor"].isNull()) {
            int sp = doc["samples_per_sensor"].as<int>();
            resizeSampleStore(sp);
            saveIntToNVSns("adc_cfg", "sps", sp);
            changed = true;
        }
        if (!doc["divider_mv"].isNull()) {
            float dv = doc["divider_mv"].is<float>() ? doc["divider_mv"].as<float>() : (float)doc["divider_mv"].as<int>();
            if (dv > 0.0f) {
                saveFloatToNVSns("adc_cfg", "divider_mv", dv);
                changed = true;
            }
        }
        bool linearChanged = false;
        if (!doc["linear_scale"].isNull()) {
            float sc = doc["linear_scale"].is<float>() ? doc["linear_scale"].as<float>() : (float)doc["linear_scale"].as<int>();
            if (isfinite(sc) && sc != 0.0f) {
                float curScale, curOffset;
                getVoltageLinearCalibration(curScale, curOffset);
                if (fabs(curScale - sc) > 0.0001f) {
                    setVoltageLinearCalibration(sc, curOffset);
                    linearChanged = true;
                }
            }
        }
        if (!doc["linear_offset"].isNull()) {
            float off = doc["linear_offset"].is<float>() ? doc["linear_offset"].as<float>() : (float)doc["linear_offset"].as<int>();
            if (isfinite(off)) {
                float curScale, curOffset;
                getVoltageLinearCalibration(curScale, curOffset);
                if (fabs(curOffset - off) > 0.0001f) {
                    setVoltageLinearCalibration(curScale, off);
                    linearChanged = true;
                }
            }
        }
        if (linearChanged) {
            changed = true;
        }
        if (!doc["divider_scale"].isNull()) {
            if (doc["divider_scale"].is<JsonArray>()) {
                JsonArray arr = doc["divider_scale"].as<JsonArray>();
                int num = min((int)arr.size(), getNumVoltageSensors());
                for (int i = 0; i < num; ++i) {
                    float sc = arr[i].is<float>() ? arr[i].as<float>() : (float)arr[i].as<double>();
                    if (isfinite(sc) && sc > 0.0f) {
                        setAdcDividerScale(i, sc);
                        changed = true;
                    }
                }
            } else if (doc["divider_scale"].is<JsonObject>()) {
                JsonObject obj = doc["divider_scale"].as<JsonObject>();
                JsonObject::iterator it = obj.begin();
                while (it != obj.end()) {
                    const char *key = it->key().c_str();
                    if (key && strncmp(key, "ai", 2) == 0) {
                        int idx = atoi(key + 2);
                        if (idx >= 0 && idx < getNumVoltageSensors()) {
                            float sc = it->value().is<float>() ? it->value().as<float>() : (float)it->value().as<double>();
                            if (isfinite(sc) && sc > 0.0f) {
                                setAdcDividerScale(idx, sc);
                                changed = true;
                            }
                        }
                    }
                    ++it;
                }
            }
        }
        if (changed) {
            auto resp = makeSuccessDoc("ADC config updated");
            sendCorsJsonDoc(request, 200, resp);
            flagSensorsSnapshotUpdate();
        } else {
            auto resp = makeErrorDoc("No supported keys provided");
            sendCorsJsonDoc(request, 400, resp);
        }
    });
    adcConfigHandler->setMaxContentLength(256);
    server->addHandler(adcConfigHandler);

    // Reseed endpoints
    server->on("/api/adc/reseed", HTTP_POST, [](AsyncWebServerRequest *request) {
        clearSampleStore();
        setupVoltagePressureSensor();
        auto resp = makeSuccessDoc("ADC smoothed values reseeded and sample buffers cleared", 192);
        sendCorsJsonDoc(request, 200, resp);
    });

    server->on("/api/ads/reseed", HTTP_POST, [](AsyncWebServerRequest *request) {
        clearAdsBuffers();
        auto resp = makeSuccessDoc("ADS buffers cleared and reseeded");
        sendCorsJsonDoc(request, 200, resp);
    });

    // SSE debug push endpoint (JSON POST). This endpoint is intentionally
    // lightweight and DOES NOT use the central sensors readings builder; it's
    // meant for quick debugging of individual sensor channels.
    AsyncCallbackJsonWebHandler* sseDebugHandler = new AsyncCallbackJsonWebHandler("/api/sse/debug", [](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject doc = json.as<JsonObject>();
        if (doc.isNull()) {
            auto resp = makeErrorDoc("Invalid JSON");
            sendCorsJsonDoc(request, 400, resp);
            return;
        }

        int pinIndex = -1;
        if (!doc["pin_index"].isNull() && doc["pin_index"].is<int>()) {
            pinIndex = doc["pin_index"].as<int>();
        } else if (!doc["tag"].isNull() && doc["tag"].is<const char*>()) {
            pinIndex = tagToIndex(String(doc["tag"].as<const char*>()));
        }

        if (pinIndex < 0 || pinIndex >= getNumVoltageSensors()) {
            auto resp = makeErrorDoc("Invalid or missing pin_index/tag");
            sendCorsJsonDoc(request, 400, resp);
            return;
        }

        // Build a small JSON payload with direct/raw reads for debugging.
        JsonDocument payload;
        payload["pin_index"] = pinIndex;
        payload["tag"] = String("AI") + String(pinIndex + 1);
        int pin = getVoltageSensorPin(pinIndex);
        payload["pin"] = pin;

        // Raw analog read
        int raw = analogRead(pin);
        payload["raw_adc"] = raw;

        // Smoothed values if available
        float smoothed = getSmoothedADC(pinIndex);
        payload["smoothed_adc"] = roundToDecimals(smoothed, 2);

        // Voltage/converted estimate — this uses per-sensor conversion but not the global readings builder
        float volt = getSmoothedVoltagePressure(pinIndex);
        payload["voltage"] = roundToDecimals(volt, 3);

        // If ADS-based sensor, include ADS smoothed mA if available
        // getAdsSmoothedMa takes channel index; we try to call it and include if positive
        float adsMa = 0.0f;
        bool hasAds = false;
        // Defensive: only call ADS function if it exists; the symbol is available when ADS is used
        adsMa = getAdsSmoothedMa(pinIndex);
        if (adsMa > 0.0f) {
            payload["ads_ma"] = roundToDecimals(adsMa, 3);
            hasAds = true;
        }

        // Serialize and push via SSE
        String out;
        serializeJson(payload, out);
        pushSseDebugMessage("sensor_debug", out);

        // Also return a quick acknowledgement
        auto resp = makeStatusDoc("sent");
        resp["event"] = "sensor_debug";
        sendCorsJsonDoc(request, 200, resp);
    });
    sseDebugHandler->setMaxContentLength(1024);
    server->addHandler(sseDebugHandler);

    // Auto-calibration endpoints previously defined in web_api.cpp are intentionally left where they were
}
