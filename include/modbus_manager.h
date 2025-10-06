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
    float smoothed_distance_mm;
    float temperature_c;
    uint16_t signal_strength;
    uint8_t last_error;
    unsigned long last_update_ms;
    float max_distance_m;
    struct ModbusRegister {
        String id;        // unique id used for DB storage (e.g. MB201_distance)
        String name;      // human-friendly name (e.g. "distance")
        String unit;      // unit string (e.g. "mm", "C")
        float raw;        // raw/unfiltered value
        float filtered;   // filtered / smoothed value if applicable
        bool valid;       // whether value is valid
    };

    // Generic per-register readings (kept in a stable order for legacy sensors)
    std::vector<ModbusRegister> registers;
};

void setupModbus();
void loopModbus();

bool applyModbusConfig(const String &json);
String getModbusConfigJson();
String getDefaultModbusConfigJson();
const std::vector<ModbusSensorData>& getModbusSensors();

// (modbus debug helper removed)

#endif // MODBUS_MANAGER_H
