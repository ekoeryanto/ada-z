#include "i2c_helpers.h"
#include "pins_config.h"
#include <Wire.h>

static bool i2cInitialized = false;

void initI2C() {
    if (i2cInitialized) return;
    // Initialize Wire with configured SDA/SCL pins
    Wire.begin(I2C_SDA, I2C_SCL);
    i2cInitialized = true;
}

void i2cScanAndLog() {
    initI2C();
    Serial.println("I2C scan starting...");
    for (uint8_t addr = 1; addr < 127; ++addr) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.print("I2C device found at 0x");
            if (addr < 16) Serial.print("0");
            Serial.println(addr, HEX);
        }
        delay(5);
    }
    Serial.println("I2C scan done.");
}
