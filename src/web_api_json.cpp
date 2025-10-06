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
#include "modbus_manager.h"
#include "json_helper.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <math.h>

namespace {

JsonObject addMeasurement(JsonArray readings, const char *name, float value, const char *unit, int decimals) {
    JsonObject meas = readings.add<JsonObject>();
    meas["name"] = name;
    if (unit && unit[0] != '\0') meas["unit"] = unit;
    if (!isnan(value)) {
        meas["value"] = roundToDecimals(value, decimals);
    } else {
        meas["status"] = "unavailable";
    }
    return meas;
}

const char *modbusDataTypeToStr(ModbusDataType type) {
    switch (type) {
        case ModbusDataType::UINT16: return "uint16";
        case ModbusDataType::INT16: return "int16";
        case ModbusDataType::UINT32: return "uint32";
        case ModbusDataType::INT32: return "int32";
        case ModbusDataType::FLOAT32: return "float32";
        default: return "unknown";
    }
}

const char *modbusRegisterTypeToStr(ModbusRegisterType type) {
    return type == ModbusRegisterType::INPUT_REGISTER ? "input" : "holding";
}

String buildModbusSensorId(const ModbusSlave &slave, const ModbusRegister &reg) {
    String base = String("MB") + String(slave.address);
    if (reg.key.length() > 0) {
        base += "." + reg.key;
    } else if (reg.label.length() > 0) {
        String label = reg.label;
        label.replace(' ', '_');
        base += "." + label;
    } else {
        base += ".reg" + String(reg.address);
    }
    return base;
}

} // namespace

// Note: this implementation reuses existing getters from the project
// (getNumVoltageSensors, getVoltageSensorPin, getSmoothedADC, etc.)
void buildSensorsReadingsJson(JsonDocument &doc) {
    doc.clear();

    const bool wifiUp = isWifiConnected();
    doc["timestamp"] = getIsoTimestamp();
    doc["rtu"] = String(getChipId());

    JsonObject net = doc["network"].to<JsonObject>();
    net["status"] = wifiUp ? "connected" : "disconnected";
    if (wifiUp) {
        net["ip"] = WiFi.localIP().toString();
        net["rssi"] = WiFi.RSSI();
    }

    JsonArray sensors = doc["sensors"].to<JsonArray>();

    const int numVoltage = getNumVoltageSensors();
    for (int i = 0; i < numVoltage; ++i) {
        bool enabled = getSensorEnabled(i);
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

        float voltageRaw = convert010V(raw);
        float voltageFiltered = convert010V(round(smoothed));
        SensorCalibration cal = getCalibrationForPin(i);
        float pressureRaw = (raw * cal.scale) + cal.offset;
        float pressureFiltered = (round(smoothed) * cal.scale) + cal.offset;

        JsonObject sensor = sensors.add<JsonObject>();
        sensor["id"] = String("AI") + String(i + 1);
        sensor["type"] = "adc";
        sensor["enabled"] = enabled ? 1 : 0;
        sensor["status"] = !enabled ? "disabled" : (saturated ? "alert" : "ok");
        sensor["port"] = pin;

        JsonObject meta = sensor["meta"].to<JsonObject>();
        meta["raw_adc"] = raw;
        meta["smoothed_adc"] = roundToDecimals(smoothed, 2);
        meta["cal_zero"] = cal.zeroPressureValue;
        meta["cal_span"] = cal.spanPressureValue;
        meta["cal_scale"] = roundToDecimals(cal.scale, 4);
        meta["cal_offset"] = roundToDecimals(cal.offset, 3);
        if (saturated) meta["saturated"] = 1;

        JsonArray readings = sensor["readings"].to<JsonArray>();
        JsonObject voltMeas = addMeasurement(readings, "voltage", voltageFiltered, "V", 3);
        if (!isnan(voltageRaw)) voltMeas["raw"] = roundToDecimals(voltageRaw, 3);

        JsonObject pressureMeas = addMeasurement(readings, "pressure", pressureFiltered, "bar", 2);
        if (!isnan(pressureRaw)) pressureMeas["raw"] = roundToDecimals(pressureRaw, 2);
    }

    for (int ch = 0; ch < 2; ++ch) {
        int16_t raw = readAdsRaw(ch);
        float mv = adsRawToMv(raw);
        float currentMa = readAdsMa(ch, DEFAULT_SHUNT_OHM, DEFAULT_AMP_GAIN);
        float depthMm = computeDepthMm(currentMa, DEFAULT_CURRENT_INIT_MA, DEFAULT_RANGE_MM, DEFAULT_DENSITY_WATER);
        float tpScale = getAdsTpScale(ch);
        float maSmoothed = getAdsSmoothedMa(ch);
        float voltageSmoothed = (maSmoothed * tpScale) / 1000.0f;
        float voltageRaw = mv / 1000.0f;
        float pressureBar = (voltageSmoothed / 10.0f) * DEFAULT_RANGE_BAR;

        JsonObject sensor = sensors.add<JsonObject>();
        sensor["id"] = String("ADS") + String(ch);
        sensor["type"] = "ads1115";
        sensor["enabled"] = 1;
        sensor["status"] = isnan(maSmoothed) ? "pending" : "ok";
        sensor["channel"] = ch;

        JsonObject meta = sensor["meta"].to<JsonObject>();
        meta["tp_scale_mv_per_ma"] = tpScale;
        meta["raw_code"] = raw;

        JsonArray readings = sensor["readings"].to<JsonArray>();
        JsonObject voltMeas = addMeasurement(readings, "voltage", voltageSmoothed, "V", 3);
        if (!isnan(voltageRaw)) voltMeas["raw"] = roundToDecimals(voltageRaw, 3);

        JsonObject currentMeas = addMeasurement(readings, "current", maSmoothed, "mA", 3);
        if (!isnan(currentMa)) currentMeas["raw"] = roundToDecimals(currentMa, 3);

        addMeasurement(readings, "pressure", pressureBar, "bar", 2);
        addMeasurement(readings, "depth", depthMm, "mm", 0);
    }

    const auto &slaves = getModbusSlaves();
    for (const auto &slave : slaves) {
        for (const auto &reg : slave.registers) {
            String sensorId = buildModbusSensorId(slave, reg);
            JsonObject sensor = sensors.add<JsonObject>();
            sensor["id"] = sensorId;
            sensor["type"] = "modbus";
            sensor["enabled"] = slave.enabled ? 1 : 0;
            if (!slave.enabled) {
                sensor["status"] = "disabled";
            } else if (!slave.online) {
                sensor["status"] = "pending";
            } else {
                sensor["status"] = isnan(reg.value) ? "pending" : "ok";
            }

            JsonObject meta = sensor["meta"].to<JsonObject>();
            meta["slave"] = slave.address;
            if (reg.label.length() > 0) meta["label"] = reg.label;
            meta["register"] = reg.address;
            meta["unit"] = reg.unit;
            meta["data_type"] = modbusDataTypeToStr(reg.data_type);
            meta["register_type"] = modbusRegisterTypeToStr(reg.reg_type);
            if (reg.last_update_ms > 0) meta["last_update_ms"] = reg.last_update_ms;

            JsonArray readings = sensor["readings"].to<JsonArray>();
            const char *name = reg.key.length() > 0 ? reg.key.c_str() : (reg.label.length() > 0 ? reg.label.c_str() : "value");
            JsonObject reading = addMeasurement(readings, name, reg.value, reg.unit.c_str(), 3);
            if (reg.unit.length() == 0) reading.remove("unit");
        }
    }

    doc["sensor_count"] = sensors.size();
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
