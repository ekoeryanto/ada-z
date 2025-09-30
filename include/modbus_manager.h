#ifndef MODBUS_MANAGER_H
#define MODBUS_MANAGER_H

#include <Arduino.h>
#include <vector>

struct ModbusSensorData {
    uint8_t address;
    String id;
    String label;
    bool online;
    float distance_mm;
    float temperature_c;
    uint16_t signal_strength;
    uint8_t last_error;
    unsigned long last_update_ms;
    float max_distance_m;
};

void setupModbus();
void loopModbus();

bool applyModbusConfig(const String &json);
String getModbusConfigJson();
String getDefaultModbusConfigJson();
const std::vector<ModbusSensorData>& getModbusSensors();

// (modbus debug helper removed)

#endif // MODBUS_MANAGER_H
