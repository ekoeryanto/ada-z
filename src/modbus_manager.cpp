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

std::vector<ModbusSlave> slaves;
size_t currentSlaveIndex = 0;
unsigned long lastPollTime = 0;
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
  "version": 2,
  "poll_interval_ms": 1000,
  "slaves": [
    {
      "address": 1,
      "label": "Example Device",
      "enabled": true,
      "registers": [
        {
          "key": "temperature",
          "label": "Temperature",
          "address": 100,
          "reg_type": "holding",
          "data_type": "int16",
          "unit": "C",
          "divisor": 10.0
        },
        {
          "key": "humidity",
          "label": "Humidity",
          "address": 101,
          "reg_type": "holding",
          "data_type": "uint16",
          "unit": "%",
          "divisor": 10.0
        }
      ]
    }
  ]
})JSON";

// Helper to convert string to enum
ModbusDataType stringToDataType(const String& str) {
    if (str.equalsIgnoreCase("int16")) return ModbusDataType::INT16;
    if (str.equalsIgnoreCase("uint32")) return ModbusDataType::UINT32;
    if (str.equalsIgnoreCase("int32")) return ModbusDataType::INT32;
    if (str.equalsIgnoreCase("float32")) return ModbusDataType::FLOAT32;
    return ModbusDataType::UINT16; // Default
}

ModbusRegisterType stringToRegisterType(const String& str) {
    if (str.equalsIgnoreCase("input")) return ModbusRegisterType::INPUT_REGISTER;
    return ModbusRegisterType::HOLDING_REGISTER; // Default
}

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

    std::vector<ModbusSlave> parsedSlaves;
    for (JsonObject slaveObj : doc["slaves"].as<JsonArray>()) {
        if (!slaveObj["address"].is<uint8_t>()) continue;

        ModbusSlave slave;
        slave.address = slaveObj["address"].as<uint8_t>();
        slave.label = slaveObj["label"].as<String>();
        slave.enabled = slaveObj["enabled"].is<bool>() ? slaveObj["enabled"].as<bool>() : true;

        if (slaveObj["registers"].is<JsonArray>()) {
            for (JsonObject regObj : slaveObj["registers"].as<JsonArray>()) {
                ModbusRegister reg;
                reg.key = regObj["key"].as<String>();
                reg.label = regObj["label"].as<String>();
                reg.address = regObj["address"].as<uint16_t>();
                reg.reg_type = stringToRegisterType(regObj["reg_type"].as<String>());
                reg.data_type = stringToDataType(regObj["data_type"].as<String>());
                reg.unit = regObj["unit"].as<String>();
                reg.divisor = regObj["divisor"].is<float>() ? regObj["divisor"].as<float>() : 1.0;
                slave.registers.push_back(reg);
            }
        }
        parsedSlaves.push_back(slave);
    }

    {
        CriticalSection guard(modbusMux);
        slaves.swap(parsedSlaves);
        if (!slaves.empty()) {
            currentSlaveIndex = currentSlaveIndex % slaves.size();
        } else {
            currentSlaveIndex = 0;
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

void setupModbus() {
    pinMode(RS485_DE, OUTPUT);
    digitalWrite(RS485_DE, LOW);

    rs485.begin(MODBUS_BAUD, SERIAL_8N1, RS485_RX, RS485_TX);
    modbusNode.preTransmission(preTransmission);
    modbusNode.postTransmission(postTransmission);

    if (getModbusConfigJson().length() == 0) {
        applyModbusConfig(getDefaultModbusConfigJson());
    }
}

void processRegisterValue(ModbusRegister& reg, uint16_t* buffer) {
    uint32_t temp_val;
    switch (reg.data_type) {
        case ModbusDataType::UINT16:
            reg.value = buffer[0];
            break;
        case ModbusDataType::INT16:
            reg.value = (int16_t)buffer[0];
            break;
        case ModbusDataType::UINT32:
            temp_val = (uint32_t)buffer[0] << 16 | buffer[1];
            reg.value = temp_val;
            break;
        case ModbusDataType::INT32:
            temp_val = (uint32_t)buffer[0] << 16 | buffer[1];
            reg.value = (int32_t)temp_val;
            break;
        case ModbusDataType::FLOAT32:
            temp_val = (uint32_t)buffer[0] << 16 | buffer[1];
            memcpy(&reg.value, &temp_val, sizeof(reg.value));
            break;
    }
    if (reg.divisor != 0) {
        reg.value /= reg.divisor;
    }
    reg.last_update_ms = millis();
}

void loopModbus() {
    unsigned long now = millis();
    if (now - lastPollTime < POLL_INTERVAL_MS) {
        return;
    }
    lastPollTime = now;

    CriticalSection guard(modbusMux);
    if (slaves.empty()) {
        return;
    }

    currentSlaveIndex = (currentSlaveIndex + 1) % slaves.size();
    ModbusSlave& currentSlave = slaves[currentSlaveIndex];

    if (!currentSlave.enabled) {
        return;
    }

    modbusNode.begin(currentSlave.address, rs485);
    bool success = false;

    for (auto& reg : currentSlave.registers) {
        uint8_t result;
        uint8_t read_len = 1;
        if (reg.data_type == ModbusDataType::UINT32 || reg.data_type == ModbusDataType::INT32 || reg.data_type == ModbusDataType::FLOAT32) {
            read_len = 2;
        }

        if (reg.reg_type == ModbusRegisterType::HOLDING_REGISTER) {
            result = modbusNode.readHoldingRegisters(reg.address, read_len);
        } else {
            result = modbusNode.readInputRegisters(reg.address, read_len);
        }

        if (result == modbusNode.ku8MBSuccess) {
            uint16_t buffer[2];
            buffer[0] = modbusNode.getResponseBuffer(0);
            if (read_len > 1) {
                buffer[1] = modbusNode.getResponseBuffer(1);
            }
            processRegisterValue(reg, buffer);
            success = true;
        } else {
            reg.value = NAN;
        }
    }

    if (success) {
        currentSlave.online = true;
        currentSlave.last_successful_comm_ms = now;
    } else {
        currentSlave.online = false;
    }
}

const std::vector<ModbusSlave>& getModbusSlaves() {
    return slaves;
}