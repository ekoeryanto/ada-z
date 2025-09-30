#include "modbus_manager.h"
#include "pins_config.h"

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
    int distanceReg;
    int temperatureReg;
    int signalReg;
    float maxDistanceM;
    String id;
    String label;
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
    DynamicJsonDocument doc(4096);
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
        cfg.distanceReg = obj.containsKey("distance_reg") ? obj["distance_reg"].as<int>() : DEFAULT_DISTANCE_REG;
        cfg.temperatureReg = obj.containsKey("temperature_reg") ? obj["temperature_reg"].as<int>() : DEFAULT_TEMPERATURE_REG;
        cfg.signalReg = obj.containsKey("signal_reg") ? obj["signal_reg"].as<int>() : DEFAULT_SIGNAL_REG;
        cfg.maxDistanceM = obj.containsKey("max_distance_m") ? obj["max_distance_m"].as<float>() : 10.0f;
        cfg.id = obj.containsKey("id") ? String(obj["id"].as<const char*>()) : String("MB") + String(cfg.address);
        cfg.label = obj.containsKey("label") ? String(obj["label"].as<const char*>()) : String();

        SensorState state;
        state.config = cfg;
        state.data.address = cfg.address;
        state.data.id = cfg.id;
        state.data.label = cfg.label;
        state.data.online = false;
        state.data.distance_mm = NAN;
        state.data.temperature_c = NAN;
        state.data.signal_strength = 0;
        state.data.last_error = 0;
        state.data.last_update_ms = 0;
        state.data.max_distance_m = cfg.maxDistanceM;
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

    if (configCopy.distanceReg >= 0) {
        uint8_t result = modbusNode.readHoldingRegisters(static_cast<uint16_t>(configCopy.distanceReg), 1);
        if (result == modbusNode.ku8MBSuccess) {
            uint16_t dist = modbusNode.getResponseBuffer(0);
            distanceMm = static_cast<float>(dist);
            distanceRead = true;
        } else {
            lastError = result;
        }
    }

    if (configCopy.temperatureReg >= 0) {
        uint8_t result = modbusNode.readHoldingRegisters(static_cast<uint16_t>(configCopy.temperatureReg), 1);
        if (result == modbusNode.ku8MBSuccess) {
            uint16_t tempRaw = modbusNode.getResponseBuffer(0);
            temperatureC = static_cast<float>(tempRaw) / 10.0f;
            temperatureRead = true;
        } else {
            lastError = result;
            temperatureC = NAN;
        }
    }

    if (configCopy.signalReg >= 0) {
        uint8_t result = modbusNode.readHoldingRegisters(static_cast<uint16_t>(configCopy.signalReg), 1);
        if (result == modbusNode.ku8MBSuccess) {
            uint16_t sig = modbusNode.getResponseBuffer(0);
            signalStrength = sig;
            signalRead = true;
        } else {
            lastError = result;
            signalStrength = 0;
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

        if (distanceRead) {
            state.data.online = true;
            state.data.distance_mm = distanceMm;
            state.data.last_error = 0;
            state.data.last_update_ms = completedAt;
        } else {
            state.data.online = false;
            state.data.distance_mm = NAN;
            state.data.last_error = lastError;
            state.data.last_update_ms = previousUpdate;
        }

        if (temperatureRead) {
            state.data.temperature_c = temperatureC;
        } else {
            state.data.temperature_c = NAN;
        }

        if (signalRead) {
            state.data.signal_strength = signalStrength;
        } else {
            state.data.signal_strength = 0;
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
