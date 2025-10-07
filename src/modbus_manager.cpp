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
#include "json_helper.h"
#include "web_api_common.h"

namespace {

constexpr uint32_t DEFAULT_MODBUS_BAUD = 9600;
uint32_t currentModbusBaud = DEFAULT_MODBUS_BAUD;
constexpr unsigned long POLL_INTERVAL_MS = 1000;
#if defined(ARDUINO_ARCH_ESP32)
constexpr uint8_t MAX_MODBUS_REG_FRAME = 64; // ModbusMaster internal buffer
#else
constexpr uint8_t MAX_MODBUS_REG_FRAME = 64;
#endif

HardwareSerial &rs485 = Serial2;
ModbusMaster modbusNode;

// Use a FreeRTOS mutex instead of portMUX (which disables interrupts) so
// blocking Modbus operations don't turn off interrupts and trip the
// Interrupt WDT. Mutex allows mutual exclusion without disabling interrupts.
SemaphoreHandle_t modbusMutex = NULL;

class CriticalSection {
public:
    // Take the provided FreeRTOS mutex (blocks indefinitely until obtained)
    explicit CriticalSection(SemaphoreHandle_t mutex) : mutex_(mutex) {
        if (mutex_) {
            xSemaphoreTake(mutex_, portMAX_DELAY);
        }
    }
    ~CriticalSection() {
        if (mutex_) {
            xSemaphoreGive(mutex_);
        }
    }

    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;

private:
    SemaphoreHandle_t mutex_;
};

std::vector<ModbusSlave> slaves;
size_t currentSlaveIndex = 0;
unsigned long lastPollTime = 0;
String currentConfigJson;

void modbusIdleTask() {
    delay(1); // Yield to keep watchdog fed while waiting on bus
}

const char* toRegisterTypeString(ModbusRegisterType type) {
    return (type == ModbusRegisterType::HOLDING_REGISTER) ? "holding" : "input";
}

const char* toPollOperationString(ModbusPollOperation op) {
    switch (op) {
        case ModbusPollOperation::READ_HOLDING:
            return "read_holding";
        case ModbusPollOperation::READ_INPUT:
            return "read_input";
        case ModbusPollOperation::WRITE_SINGLE:
            return "write_single";
        case ModbusPollOperation::WRITE_MULTIPLE:
            return "write_multiple";
    }
    return "unknown";
}

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
  "baud_rate": 9600,
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

    // If baud_rate is specified, use it. Otherwise, fall back to the default.
    // This ensures that a config without a baud_rate key will reset the baud rate to default.
    uint32_t newBaud = doc["baud_rate"].isNull() ? DEFAULT_MODBUS_BAUD : doc["baud_rate"].as<uint32_t>();
    if (newBaud > 0 && newBaud != currentModbusBaud) {
        currentModbusBaud = newBaud;
        rs485.end();
        rs485.begin(currentModbusBaud, SERIAL_8N1, RS485_RX, RS485_TX);
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
                reg.id = regObj["id"].as<String>();
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
        CriticalSection guard(modbusMutex);
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

    flagSensorsSnapshotUpdate();
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

    rs485.begin(currentModbusBaud, SERIAL_8N1, RS485_RX, RS485_TX);
    modbusNode.preTransmission(preTransmission);
    modbusNode.postTransmission(postTransmission);
    modbusNode.idle(modbusIdleTask);

    // Create the mutex used to protect Modbus operations. Use a normal binary
    // semaphore (mutex) so we don't disable interrupts while the bus is in use.
    if (modbusMutex == NULL) {
        modbusMutex = xSemaphoreCreateMutex();
    }

    if (getModbusConfigJson().length() == 0) {
        applyModbusConfig(getDefaultModbusConfigJson());
    }
}

void processRegisterValue(ModbusRegister& reg, uint16_t* buffer) {
    float previous = reg.value;
    bool previousNan = isnan(previous);
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

    bool currentNan = isnan(reg.value);
    bool valueChanged = false;
    if (previousNan != currentNan) {
        valueChanged = true;
    } else if (!currentNan) {
        float diff = fabs(reg.value - previous);
        if (diff > 0.0005f) {
            valueChanged = true;
        }
    }

    if (valueChanged) {
        flagSensorsSnapshotUpdate();
    }
}

void loopModbus() {
    unsigned long now = millis();
    if (now - lastPollTime < POLL_INTERVAL_MS) {
        return;
    }
    lastPollTime = now;

    CriticalSection guard(modbusMutex);
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
            float previous = reg.value;
            reg.value = NAN;
            reg.last_update_ms = millis();
            if (!isnan(previous)) {
                flagSensorsSnapshotUpdate();
            }
        }
    }

    if (success) {
        currentSlave.online = true;
        currentSlave.last_successful_comm_ms = now;
    } else {
        currentSlave.online = false;
    }

    if (!success) {
        flagSensorsSnapshotUpdate();
    }
}

const std::vector<ModbusSlave>& getModbusSlaves() {
    return slaves;
}

String pollModbus(const ModbusPollRequest& request) {
    CriticalSection guard(modbusMutex); // Ensure exclusive access to the bus

    bool baudChanged = false;
    if (request.baudRate > 0 && request.baudRate != currentModbusBaud) {
        baudChanged = true;
        rs485.end();
        rs485.begin(request.baudRate, SERIAL_8N1, RS485_RX, RS485_TX);
    }

    modbusNode.begin(request.slaveAddress, rs485);

    uint8_t result = modbusNode.ku8MBSuccess;
    ModbusRegisterType regTypeForRead = ModbusRegisterType::HOLDING_REGISTER;
    uint16_t effectiveCount = request.count;

    switch (request.operation) {
        case ModbusPollOperation::READ_HOLDING:
            regTypeForRead = ModbusRegisterType::HOLDING_REGISTER;
            modbusNode.clearResponseBuffer();
            result = modbusNode.readHoldingRegisters(request.registerAddress, effectiveCount);
            break;
        case ModbusPollOperation::READ_INPUT:
            regTypeForRead = ModbusRegisterType::INPUT_REGISTER;
            modbusNode.clearResponseBuffer();
            result = modbusNode.readInputRegisters(request.registerAddress, effectiveCount);
            break;
        case ModbusPollOperation::WRITE_SINGLE:
            if (!request.values.empty()) {
                result = modbusNode.writeSingleRegister(request.registerAddress, request.values[0]);
            } else {
                result = 0xFF; // Custom error code for invalid payload
            }
            break;
        case ModbusPollOperation::WRITE_MULTIPLE:
            if (!request.values.empty()) {
                if (request.values.size() > MAX_MODBUS_REG_FRAME) {
                    result = ModbusMaster::ku8MBIllegalDataAddress;
                    break;
                }
                modbusNode.clearTransmitBuffer();
                for (size_t i = 0; i < request.values.size(); ++i) {
                    modbusNode.setTransmitBuffer(i, request.values[i]);
                }
                effectiveCount = static_cast<uint16_t>(request.values.size());
                result = modbusNode.writeMultipleRegisters(request.registerAddress, effectiveCount);
            } else {
                result = 0xFF; // Custom error code for invalid payload
            }
            break;
    }

    if (baudChanged) {
        rs485.end();
        rs485.begin(currentModbusBaud, SERIAL_8N1, RS485_RX, RS485_TX);
    }

    const size_t maxItems = (request.operation == ModbusPollOperation::WRITE_SINGLE) ? 1 :
                            (request.operation == ModbusPollOperation::WRITE_MULTIPLE ? request.values.size() : effectiveCount);
    const size_t baseCapacity = 256 + (maxItems * 12);
    auto doc = makeSuccessDoc("", baseCapacity);
    doc["operation"] = toPollOperationString(request.operation);
    doc["slave_address"] = request.slaveAddress;
    doc["register_address"] = request.registerAddress;
    if (request.baudRate > 0) {
        doc["baud_rate"] = request.baudRate;
    }

    if (request.operation == ModbusPollOperation::READ_HOLDING || request.operation == ModbusPollOperation::READ_INPUT) {
        doc["register_type"] = toRegisterTypeString(regTypeForRead);
        doc["count"] = effectiveCount;
    } else {
        doc["write_count"] = maxItems;
    }

    if (result == modbusNode.ku8MBSuccess) {
        if (request.operation == ModbusPollOperation::READ_HOLDING ||
            request.operation == ModbusPollOperation::READ_INPUT) {
            JsonArray data = doc["data"].to<JsonArray>();
            for (uint16_t i = 0; i < effectiveCount; i++) {
                data.add(modbusNode.getResponseBuffer(i));
            }
        } else {
            JsonArray valuesArray = doc["values"].to<JsonArray>();
            if (request.operation == ModbusPollOperation::WRITE_SINGLE && !request.values.empty()) {
                valuesArray.add(request.values[0]);
            } else {
                for (uint16_t value : request.values) {
                    valuesArray.add(value);
                }
            }
        }
    } else {
        String errMsg;
        if (result == 0xFF) {
            errMsg = F("Invalid Modbus payload");
        } else {
            errMsg = String(F("Modbus error: 0x")) + String(result, HEX);
        }
        setStatusMessage(doc, "error", errMsg);
        doc["error_code"] = result;
    }

    String output;
    serializeJson(doc, output);
    return output;
}
