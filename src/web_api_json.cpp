#include "web_api_json.h"
#include "web_api_common.h"
#include "voltage_pressure_sensor.h"
#include "sensor_calibration_types.h"
#include "current_pressure_sensor.h"
#include "config.h"
#include "sample_store.h"
#include "sensors_config.h"
#include "time_sync.h"
#include "device_id.h"
#include "wifi_manager_module.h"
#include "json_helper.h"
#include <WiFi.h>
#include <ArduinoJson.h>

// Note: this implementation reuses existing getters from the project
// (getNumVoltageSensors, getVoltageSensorPin, getSmoothedADC, etc.)
void buildSensorsReadingsJson(JsonDocument &doc) {
    int n = getNumVoltageSensors();
    const int ads_channels = 2;

    // Timestamp and network info
    doc["timestamp"] = getIsoTimestamp();
    {
        JsonObject net = doc["network"].to<JsonObject>();
        net["connected"] = isWifiConnected() ? 1 : 0;
        net["ip"] = isWifiConnected() ? WiFi.localIP().toString() : String("");
        net["gateway"] = isWifiConnected() ? WiFi.gatewayIP().toString() : String("");
        net["mac"] = WiFi.macAddress();
        net["rssi"] = isWifiConnected() ? WiFi.RSSI() : 0;
    }
    doc["rtu"] = String(getChipId());
    JsonArray tags = doc["tags"].to<JsonArray>();

    for (int i = 0; i < n; ++i) {
        int pin = getVoltageSensorPin(i);
        int raw = analogRead(pin);
        float smoothed = getSmoothedADC(i);
        float avgRawF, avgSmoothedF, avgVoltF;
        if (getAverages(i, avgRawF, avgSmoothedF, avgVoltF)) {
            raw = (int)round(avgRawF);
            smoothed = avgSmoothedF;
        }
        bool saturated = isPinSaturated(i);
        if (raw == 4095 && !saturated) raw = (int)round(smoothed);
        float voltage_smoothed_v = convert010V(round(smoothed));
        struct SensorCalibration cal = getCalibrationForPin(i);
        float pressure_from_raw = (raw * cal.scale) + cal.offset;
        float pressure_from_smoothed = (round(smoothed) * cal.scale) + cal.offset;
        float pressure_final = pressure_from_smoothed;

        JsonObject s = tags.add<JsonObject>();
        s["id"] = String("AI") + String(i + 1);
        s["port"] = pin;
        s["index"] = i;
        s["source"] = "adc";
        s["enabled"] = getSensorEnabled(i) ? 1 : 0;

        JsonObject val = s["value"].to<JsonObject>();
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

        JsonObject meta = s["meta"].to<JsonObject>();
        meta["cal_zero_raw_adc"] = cal.zeroRawAdc;
        meta["cal_span_raw_adc"] = cal.spanRawAdc;
        meta["cal_zero_pressure_value"] = cal.zeroPressureValue;
        meta["cal_span_pressure_value"] = cal.spanPressureValue;
        meta["cal_scale"] = cal.scale;
        meta["cal_offset"] = cal.offset;
        meta["saturated"] = saturated ? 1 : 0;
    }

    for (int ch = 0; ch <= 1; ++ch) {
        int16_t raw = readAdsRaw(ch);
        float mv = adsRawToMv(raw);
        float tp_scale = getAdsTpScale(ch);
        float ma = readAdsMa(ch, DEFAULT_SHUNT_OHM, DEFAULT_AMP_GAIN);
        float depth_mm = computeDepthMm(ma, DEFAULT_CURRENT_INIT_MA, DEFAULT_RANGE_MM, DEFAULT_DENSITY_WATER);
        float voltage_v = mv / 1000.0f;
        float pressure_bar = (voltage_v / 10.0f) * DEFAULT_RANGE_BAR;

        JsonObject s = tags.add<JsonObject>();
        s["id"] = String("ADS_A") + String(ch);
        s["port"] = ch;
        s["index"] = n + ch;
        s["source"] = "ads1115";

        JsonObject val = s["value"].to<JsonObject>();
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
        JsonObject convA = val["converted"].to<JsonObject>();
        convA["value"] = roundToDecimals(pressure_bar, 2);
        convA["unit"] = "bar";
        convA["semantic"] = "pressure";
        convA["note"] = "TP5551 derived";
        convA["raw"] = roundToDecimals(pressure_bar, 2);
        float pressure_from_smoothed = (voltage_from_smoothed / 10.0f) * DEFAULT_RANGE_BAR;
        convA["filtered"] = roundToDecimals(pressure_from_smoothed, 2);

        JsonObject audit = val["audit"].to<JsonObject>();
        float measured_voltage_ads_v = mv / 1000.0f;
        float expected_voltage_ads_v = (convA["value"].is<float>() ? convA["value"].as<float>() : (float)convA["value"].as<int>()) / DEFAULT_RANGE_BAR * 10.0f;
        audit["expected_voltage_ads_v"] = roundToDecimals(expected_voltage_ads_v, 3);
        audit["measured_voltage_v"] = roundToDecimals(measured_voltage_ads_v, 3);
        audit["expected_voltage_v_from_pressure"] = roundToDecimals(expected_voltage_ads_v, 3);
        audit["voltage_delta_v"] = roundToDecimals(measured_voltage_ads_v - expected_voltage_ads_v, 3);

        JsonObject meta = s["meta"].to<JsonObject>();
        JsonObject meta_meas = meta["measurement"].to<JsonObject>();
        meta_meas["mv"] = mv;
        meta_meas["ma"] = ma;
        meta["tp_model"] = String("TP5551");
        meta["tp_scale_mv_per_ma"] = tp_scale;
        meta["cal_tp_scale_mv_per_ma"] = tp_scale;
        meta["ma_smoothed"] = ma_smoothed;
        meta["depth_mm"] = depth_mm;
    }

    doc["tags_total"] = tags.size();
}

void buildCalibrationJsonForPin(JsonDocument &doc, int pinIndex) {
    struct SensorCalibration cal = getCalibrationForPin(pinIndex);
    doc["pin_index"] = pinIndex;
    doc["pin"] = getVoltageSensorPin(pinIndex);
    doc["tag"] = String("AI") + String(pinIndex + 1);
    doc["zero_raw_adc"] = cal.zeroRawAdc;
    doc["span_raw_adc"] = cal.spanRawAdc;
    doc["zero_pressure_value"] = cal.zeroPressureValue;
    doc["span_pressure_value"] = cal.spanPressureValue;
    doc["scale"] = cal.scale;
    doc["offset"] = cal.offset;
}
