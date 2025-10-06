#pragma once

#include <Arduino.h>

// Initialize I2C (Wire) with pins from pins_config.h
// safe to call multiple times; will set Wire to configured SDA/SCL once
void initI2C();

// Optional: perform a quick I2C bus scan and print found addresses to Serial
void i2cScanAndLog();
