#include "modbus_manager.h"
#include "pins_config.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <ArduinoJson.h>
#include <ModbusMaster.h>
#include <math.h>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {

constexpr uint32_t MODBUS_BAUD = 9600;
constexpr unsigned long POLL_INTERVAL_MS = 1000;
constexpr int DEFAULT_DISTANCE_REG = 0x0101;
constexpr int DEFAULT_TEMPERATURE_REG = 0x0002;
constexpr int DEFAULT_SIGNAL_REG = 0x0003;

HardwareSerial &rs485 = Serial2;
ModbusMaster modbusNode;

portMUX_TYPE modbusMux = portMUX_INITIALIZER_UNLOCKED;

class CriticalSection {
public:
    explicit CriticalSection(portMUX_TYPE &mux) : mux_(&mux) { portENTER_CRITICAL(mux_); }
    ~CriticalSection() { portEXIT_CRITICAL(mux_); }

    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;

private:
    portMUX_TYPE *mux_;
};

struct SensorConfig {
    uint8_t address;
    struct RegisterConfig {
        String id;    // unique id for DB
        String name;  // human name
        String unit;  // unit string
        int reg;      // starting register
        int count;    // number of registers to read
        int type;     // interpretation (0=u16,1=s16,2=u32,3=s32,4=float32)
        float scale;  // optional scale applied to raw
        float ema_alpha; // optional per-register EMA alpha (0=no per-register EMA)
    };
    std::vector<RegisterConfig> registers; // optional dynamic registers list
    int distanceReg;
    int distanceRegCount;
    // interpretation of registers: 0=u16,1=s16,2=u32,3=s32,4=float32
    int distanceRegType;
    int temperatureReg;
    int temperatureRegCount;
    int temperatureRegType;
    int signalReg;
    int signalRegCount;
    int signalRegType;
    float maxDistanceM;
    String id;
    String label;
    float ema_alpha; // 0.0 to 1.0, 0 = disabled
};

struct SensorState {
    SensorConfig config;
    ModbusSensorData data;
    unsigned long lastRequestMs = 0;
};

std::vector<SensorState> sensors;
size_t currentSensorIndex = 0;
String currentConfigJson;

void preTransmission() {
    digitalWrite(RS485_DE, HIGH);
    delayMicroseconds(10);
}

void postTransmission() {
    delayMicroseconds(10);
    digitalWrite(RS485_DE, LOW);
}

const char DEFAULT_MODBUS_CONFIG[] PROGMEM = R"JSON({
  "version": 1,
  "slaves": [
        {
            "id": "MB201",
            "label": "Ultrasonik 1",
            "address": 201,
            "distance_reg": 257,
            "temperature_reg": 2,
            "signal_reg": 3,
            "max_distance_m": 10.0
        },
        {
            "id": "MB202",
            "label": "Ultrasonik 2",
            "address": 202,
            "distance_reg": 257,
            "temperature_reg": 2,
            "signal_reg": 3,
            "max_distance_m": 10.0
        }
  ]
})JSON";

} // namespace

String getDefaultModbusConfigJson() {
    return String(FPSTR(DEFAULT_MODBUS_CONFIG));
}

bool applyModbusConfig(const String &json) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        return false;
    }
    if (!doc["slaves"].is<JsonArray>()) {
        return false;
    }

    std::vector<SensorState> parsed;
    for (JsonObject obj : doc["slaves"].as<JsonArray>()) {
        if (!obj["address"].is<uint16_t>()) {
            return false;
        }
        SensorConfig cfg;
        cfg.address = static_cast<uint8_t>(obj["address"].as<uint16_t>());
    cfg.distanceReg = obj["distance_reg"].is<int>() ? obj["distance_reg"].as<int>() : DEFAULT_DISTANCE_REG;
    cfg.distanceRegCount = obj["distance_reg_count"].is<int>() ? obj["distance_reg_count"].as<int>() : 1;
    cfg.distanceRegType = obj["distance_reg_type"].is<int>() ? obj["distance_reg_type"].as<int>() : 0;

    cfg.temperatureReg = obj["temperature_reg"].is<int>() ? obj["temperature_reg"].as<int>() : DEFAULT_TEMPERATURE_REG;
    cfg.temperatureRegCount = obj["temperature_reg_count"].is<int>() ? obj["temperature_reg_count"].as<int>() : 1;
    cfg.temperatureRegType = obj["temperature_reg_type"].is<int>() ? obj["temperature_reg_type"].as<int>() : 0;

    cfg.signalReg = obj["signal_reg"].is<int>() ? obj["signal_reg"].as<int>() : DEFAULT_SIGNAL_REG;
    cfg.signalRegCount = obj["signal_reg_count"].is<int>() ? obj["signal_reg_count"].as<int>() : 1;
    cfg.signalRegType = obj["signal_reg_type"].is<int>() ? obj["signal_reg_type"].as<int>() : 0;
        cfg.maxDistanceM = obj["max_distance_m"].is<float>() ? obj["max_distance_m"].as<float>() : 10.0f;
        cfg.id = obj["id"].is<const char*>() ? String(obj["id"].as<const char*>()) : String("MB") + String(cfg.address);
        cfg.label = obj["label"].is<const char*>() ? String(obj["label"].as<const char*>()) : String();
    cfg.ema_alpha = obj["ema_alpha"].is<float>() ? obj["ema_alpha"].as<float>() : 0.0f;

        SensorState state;
        state.config = cfg;
        state.data.address = cfg.address;
        state.data.id = cfg.id;
        state.data.label = cfg.label;
        state.data.online = false;
        state.data.distance_mm = NAN;
        state.data.smoothed_distance_mm = NAN;
        state.data.temperature_c = NAN;
        state.data.signal_strength = 0;
        state.data.last_error = 0;
        state.data.last_update_ms = 0;
        state.data.max_distance_m = cfg.maxDistanceM;
        // prepare runtime registers vector
        state.data.registers.clear();

        // If the incoming JSON provided an explicit registers[] array, parse it into cfg.registers and
        // create corresponding ModbusSensorData::ModbusRegister entries. Otherwise keep legacy mapping
        // (distance, temperature, signal) populated from cfg.distanceReg etc.
        if (obj["registers"].is<JsonArray>()) {
            for (JsonObject rj : obj["registers"].as<JsonArray>()) {
                SensorConfig::RegisterConfig rc;
                rc.id = rj["id"].is<const char*>() ? String(rj["id"].as<const char*>()) : String();
                rc.name = rj["name"].is<const char*>() ? String(rj["name"].as<const char*>()) : String();
                rc.unit = rj["unit"].is<const char*>() ? String(rj["unit"].as<const char*>()) : String();
                rc.reg = rj["reg"].is<int>() ? rj["reg"].as<int>() : -1;
                rc.count = rj["count"].is<int>() ? rj["count"].as<int>() : 1;
                rc.type = rj["type"].is<int>() ? rj["type"].as<int>() : 0;
                rc.scale = rj["scale"].is<float>() ? rj["scale"].as<float>() : 1.0f;
                rc.ema_alpha = rj["ema_alpha"].is<float>() ? rj["ema_alpha"].as<float>() : 0.0f;
                // push into sensor config
                state.config.registers.push_back(rc);

                // create runtime ModbusRegister
                ModbusSensorData::ModbusRegister mr;
                if (rc.id.length() > 0) mr.id = rc.id; else mr.id = String(cfg.id) + String("_") + rc.name;
                mr.name = rc.name.length() ? rc.name : String("reg_") + String(rc.reg);
                mr.unit = rc.unit.length() ? rc.unit : String("");
                mr.raw = NAN;
                mr.filtered = NAN;
                mr.valid = false;
                state.data.registers.push_back(mr);
            }
        } else {
            // legacy: distance, temperature, signal mapping
            // distance
            {
                ModbusSensorData::ModbusRegister r;
                r.id = String(cfg.id) + String("_distance");
                r.name = String("distance");
                r.unit = String("mm");
                r.raw = NAN;
                r.filtered = NAN;
                r.valid = false;
                state.data.registers.push_back(r);
            }
            // temperature
            {
                ModbusSensorData::ModbusRegister r;
                r.id = String(cfg.id) + String("_temperature");
                r.name = String("temperature");
                r.unit = String("C");
                r.raw = NAN;
                r.filtered = NAN;
                r.valid = false;
                state.data.registers.push_back(r);
            }
            // signal
            {
                ModbusSensorData::ModbusRegister r;
                r.id = String(cfg.id) + String("_signal");
                r.name = String("signal");
                r.unit = String("pct");
                r.raw = NAN;
                r.filtered = NAN;
                r.valid = false;
                state.data.registers.push_back(r);
            }
        }
        state.lastRequestMs = 0;

        parsed.push_back(std::move(state));
    }

    {
        CriticalSection guard(modbusMux);
        sensors.swap(parsed);
        if (!sensors.empty()) {
            currentSensorIndex = currentSensorIndex % sensors.size();
        } else {
            currentSensorIndex = 0;
        }
    }

    String canonical;
    serializeJson(doc, canonical);
    currentConfigJson = canonical;
    return true;
}

String getModbusConfigJson() {
    if (currentConfigJson.length() == 0) {
        return getDefaultModbusConfigJson();
    }
    return currentConfigJson;
}

// (modbus debug helper removed)

void setupModbus() {
    pinMode(RS485_DE, OUTPUT);
    digitalWrite(RS485_DE, LOW);

    rs485.begin(MODBUS_BAUD, SERIAL_8N1, RS485_RX, RS485_TX);
    modbusNode.preTransmission(preTransmission);
    modbusNode.postTransmission(postTransmission);

    bool haveSensors;
    {
        CriticalSection guard(modbusMux);
        haveSensors = !sensors.empty();
    }

    if (!haveSensors) {
        applyModbusConfig(getDefaultModbusConfigJson());
    }
}

void loopModbus() {
    unsigned long now = millis();

    SensorConfig configCopy;
    size_t sensorIndex = 0;
    bool shouldPoll = false;

    {
        CriticalSection guard(modbusMux);
        if (sensors.empty()) {
            currentSensorIndex = 0;
            return;
        }

        SensorState &state = sensors[currentSensorIndex];
        if (now - state.lastRequestMs < POLL_INTERVAL_MS) {
            currentSensorIndex = (currentSensorIndex + 1) % sensors.size();
            return;
        }

        state.lastRequestMs = now;
        configCopy = state.config;
        sensorIndex = currentSensorIndex;
        shouldPoll = true;
    }

    if (!shouldPoll) {
        return;
    }

    modbusNode.begin(configCopy.address, rs485);

    float distanceMm = NAN;
    float temperatureC = NAN;
    uint16_t signalStrength = 0;
    bool distanceRead = false;
    bool temperatureRead = false;
    bool signalRead = false;
    uint8_t lastError = 0;

    auto readRegisterValue = [&](int reg, int count, int type, float &out, uint8_t &err)->bool {
        if (reg < 0) { err = 0; return false; }
        uint8_t result = modbusNode.readHoldingRegisters(static_cast<uint16_t>(reg), (count <= 0) ? 1 : count);
        if (result != modbusNode.ku8MBSuccess) {
            err = result;
            return false;
        }
        if ((count <= 0) || (count == 1)) {
            uint16_t v = modbusNode.getResponseBuffer(0);
            switch (type) {
                case 1: // s16
                    out = static_cast<int16_t>(v);
                    break;
                default: // u16
                    out = static_cast<uint16_t>(v);
                    break;
            }
            return true;
        } else {
            // count >= 2 -> assemble 32-bit
            uint32_t hi = modbusNode.getResponseBuffer(0);
            uint32_t lo = modbusNode.getResponseBuffer(1);
            uint32_t combined = (hi << 16) | lo;
            switch (type) {
                case 2: // u32
                    out = static_cast<float>(combined);
                    break;
                case 3: { // s32
                    out = static_cast<float>(static_cast<int32_t>(combined));
                    break; }
                case 4: { // float32 (assume Modbus Big-Endian registers)
                    union { uint32_t u; float f; } u; u.u = combined; out = u.f; break; }
                default:
                    out = static_cast<float>(combined);
                    break;
            }
            return true;
        }
    };

    // If dynamic registers defined, iterate them and update runtime registers accordingly.
    std::vector<float> regTmp;
    std::vector<uint8_t> regErr;
    std::vector<bool> regOk;
    if (!configCopy.registers.empty()) {
        regTmp.resize(configCopy.registers.size());
        regErr.resize(configCopy.registers.size());
        regOk.resize(configCopy.registers.size());
        for (size_t ri = 0; ri < configCopy.registers.size(); ++ri) {
            const auto &rc = configCopy.registers[ri];
            float tmp = 0.0f;
            uint8_t rerr = 0;
            bool ok = readRegisterValue(rc.reg, rc.count, rc.type, tmp, rerr);
            // Apply scale if ok
            if (ok) tmp = tmp * rc.scale;

            regTmp[ri] = tmp;
            regErr[ri] = rerr;
            regOk[ri] = ok;

            // Update local runtime variables and flags for legacy mapping if name matches
            String lname = rc.name;
            lname.toLowerCase();
            if (ok) {
                if (lname == "distance" || lname == "distance_mm") {
                    distanceMm = tmp;
                    distanceRead = true;
                } else if (lname == "temperature" || lname == "temperature_c") {
                    temperatureC = tmp;
                    temperatureRead = true;
                } else if (lname == "signal" || lname == "signal_strength") {
                    signalStrength = static_cast<uint16_t>(tmp);
                    signalRead = true;
                }
            } else {
                lastError = rerr;
            }
        }
    } else {
        // Legacy behavior: read distance/temperature/signal as before
        if (configCopy.distanceReg >= 0) {
            float tmp = 0.0f;
            uint8_t rerr = 0;
            if (readRegisterValue(configCopy.distanceReg, configCopy.distanceRegCount, configCopy.distanceRegType, tmp, rerr)) {
                distanceMm = tmp;
                distanceRead = true;
            } else {
                lastError = rerr;
            }
        }

        if (configCopy.temperatureReg >= 0) {
            float tmp = 0.0f;
            uint8_t rerr = 0;
            if (readRegisterValue(configCopy.temperatureReg, configCopy.temperatureRegCount, configCopy.temperatureRegType, tmp, rerr)) {
                // if registers are integer tenths, keep backward-compatible scaling when type is 0/1
                if (configCopy.temperatureRegCount <= 1 && (configCopy.temperatureRegType == 0 || configCopy.temperatureRegType == 1)) {
                    temperatureC = tmp / 10.0f;
                } else {
                    temperatureC = tmp;
                }
                temperatureRead = true;
            } else {
                lastError = rerr;
                temperatureC = NAN;
            }
        }

        if (configCopy.signalReg >= 0) {
            float tmp = 0.0f;
            uint8_t rerr = 0;
            if (readRegisterValue(configCopy.signalReg, configCopy.signalRegCount, configCopy.signalRegType, tmp, rerr)) {
                signalStrength = static_cast<uint16_t>(tmp);
                signalRead = true;
            } else {
                lastError = rerr;
                signalStrength = 0;
            }
        }
    }

    unsigned long completedAt = millis();

    {
        CriticalSection guard(modbusMux);
        size_t sensorCount = sensors.size();
        if (sensorCount == 0 || sensorIndex >= sensorCount) {
            currentSensorIndex = 0;
            return;
        }

        SensorState &state = sensors[sensorIndex];
        if (state.config.address != configCopy.address) {
            currentSensorIndex = sensorCount > 0 ? (sensorIndex % sensorCount) : 0;
            return;
        }

        unsigned long previousUpdate = state.data.last_update_ms;

        state.data.address = configCopy.address;
        state.data.id = configCopy.id;
        state.data.label = configCopy.label;
        state.data.max_distance_m = configCopy.maxDistanceM;

        // If dynamic registers defined, update them from configCopy.registers
        if (!configCopy.registers.empty()) {
            state.data.last_update_ms = completedAt;
            state.data.last_error = 0;
            // Ensure sizes align
            size_t upto = std::min(state.data.registers.size(), configCopy.registers.size());
            for (size_t i = 0; i < upto; ++i) {
                const auto &rc = configCopy.registers[i];
                bool ok = regOk[i];
                float tmp = regTmp[i];
                uint8_t rerr = regErr[i];

                if (ok) tmp = tmp * rc.scale;

                state.data.registers[i].raw = ok ? tmp : NAN;
                // apply per-register EMA if configured, otherwise default to config.ema_alpha
                float alpha = (rc.ema_alpha > 0.0f) ? rc.ema_alpha : configCopy.ema_alpha;
                if (ok) {
                    if (alpha > 0.0f) {
                        if (isnan(state.data.registers[i].filtered)) state.data.registers[i].filtered = tmp;
                        else state.data.registers[i].filtered = (alpha * tmp) + ((1.0f - alpha) * state.data.registers[i].filtered);
                    } else {
                        state.data.registers[i].filtered = tmp;
                    }
                    state.data.registers[i].valid = true;
                } else {
                    state.data.registers[i].valid = false;
                    // record lastError if any
                    state.data.last_error = rerr;
                }

                // map into legacy fields if name matches known names
                String lname = state.data.registers[i].name;
                lname.toLowerCase();
                if (lname == "distance" || lname == "distance_mm") {
                    if (state.data.registers[i].valid) {
                        state.data.distance_mm = state.data.registers[i].raw;
                        state.data.online = true;
                        // update smoothed_distance_mm using same alpha
                        if (alpha > 0.0f) {
                            if (isnan(state.data.smoothed_distance_mm)) state.data.smoothed_distance_mm = state.data.registers[i].raw;
                            else state.data.smoothed_distance_mm = (alpha * state.data.registers[i].raw) + ((1.0f - alpha) * state.data.smoothed_distance_mm);
                        } else {
                            state.data.smoothed_distance_mm = state.data.registers[i].raw;
                        }
                    } else {
                        state.data.distance_mm = NAN;
                    }
                } else if (lname == "temperature" || lname == "temperature_c") {
                    state.data.temperature_c = state.data.registers[i].valid ? state.data.registers[i].raw : NAN;
                } else if (lname == "signal" || lname == "signal_strength") {
                    state.data.signal_strength = state.data.registers[i].valid ? static_cast<uint16_t>(state.data.registers[i].raw) : 0;
                }
            }
        } else {
            // legacy field updates (unchanged)
            if (distanceRead) {
                state.data.online = true;
                state.data.distance_mm = distanceMm;
                state.data.last_error = 0;
                state.data.last_update_ms = completedAt;

                // Update generic register entry 0 = distance
                if (state.data.registers.size() > 0) {
                    state.data.registers[0].raw = distanceMm;
                    // apply EMA smoothing into filtered same as legacy
                    if (configCopy.ema_alpha > 0.0f) {
                        if (isnan(state.data.registers[0].filtered)) {
                            state.data.registers[0].filtered = distanceMm;
                        } else {
                            state.data.registers[0].filtered = (configCopy.ema_alpha * distanceMm) + ((1.0f - configCopy.ema_alpha) * state.data.registers[0].filtered);
                        }
                    } else {
                        state.data.registers[0].filtered = distanceMm;
                    }
                    state.data.registers[0].valid = true;
                }

                // Apply EMA filter if enabled
                if (configCopy.ema_alpha > 0.0f) {
                    if (isnan(state.data.smoothed_distance_mm)) {
                        state.data.smoothed_distance_mm = distanceMm; // Initialize
                    } else {
                        state.data.smoothed_distance_mm = (configCopy.ema_alpha * distanceMm) + ((1.0f - configCopy.ema_alpha) * state.data.smoothed_distance_mm);
                    }
                } else {
                    state.data.smoothed_distance_mm = distanceMm; // No filter, use raw value
                }
            } else {
                state.data.online = false;
                state.data.distance_mm = NAN;
                // Do not update smoothed value on error, keep last known good value
                state.data.last_error = lastError;
                state.data.last_update_ms = previousUpdate;
                if (state.data.registers.size() > 0) {
                    state.data.registers[0].valid = false;
                }
            }

            if (temperatureRead) {
                state.data.temperature_c = temperatureC;
                if (state.data.registers.size() > 1) {
                    state.data.registers[1].raw = temperatureC;
                    state.data.registers[1].filtered = temperatureC;
                    state.data.registers[1].valid = true;
                }
            } else {
                state.data.temperature_c = NAN;
                if (state.data.registers.size() > 1) {
                    state.data.registers[1].valid = false;
                }
            }

            if (signalRead) {
                state.data.signal_strength = signalStrength;
                if (state.data.registers.size() > 2) {
                    state.data.registers[2].raw = static_cast<float>(signalStrength);
                    state.data.registers[2].filtered = static_cast<float>(signalStrength);
                    state.data.registers[2].valid = true;
                }
            } else {
                state.data.signal_strength = 0;
                if (state.data.registers.size() > 2) {
                    state.data.registers[2].valid = false;
                }
            }
        }

        currentSensorIndex = sensorCount > 0 ? ((sensorIndex + 1) % sensorCount) : 0;
    }
}

const std::vector<ModbusSensorData>& getModbusSensors() {
    static std::vector<ModbusSensorData> cache;
    cache.clear();
    {
        CriticalSection guard(modbusMux);
        cache.reserve(sensors.size());
        for (const auto &state : sensors) {
            cache.push_back(state.data);
        }
    }
    return cache;
}
