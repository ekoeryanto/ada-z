#pragma once

#include <Arduino.h>

// Return a compact chip id derived from the ESP32 MAC/efuse (6 hex digits, uppercase)
String getChipId();
