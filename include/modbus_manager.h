#ifndef MODBUS_MANAGER_H
#define MODBUS_MANAGER_H

#include <Arduino.h>
#include <vector>
#include <map>

enum class ModbusDataType {
    UINT16,
    INT16,
    UINT32,
    INT32,
    FLOAT32
};

enum class ModbusRegisterType {
    HOLDING_REGISTER,
    INPUT_REGISTER
};

struct ModbusRegister {
    String key;
    String label;
    uint16_t address;
    ModbusRegisterType reg_type;
    ModbusDataType data_type;
    String unit;
    float divisor = 1.0;
    
    // Value
    float value = NAN;
    unsigned long last_update_ms = 0;
};

struct ModbusSlave {
    uint8_t address;
    String label;
    bool enabled = true;
    bool online = false;
    unsigned long last_successful_comm_ms = 0;
    std::vector<ModbusRegister> registers;
};

void setupModbus();
void loopModbus();

bool applyModbusConfig(const String &json);
String getModbusConfigJson();
String getDefaultModbusConfigJson();

const std::vector<ModbusSlave>& getModbusSlaves();

#endif // MODBUS_MANAGER_H