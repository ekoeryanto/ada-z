#include "calibration_keys.h"

const char* CAL_NAMESPACE = "calibration";
const char* CAL_OFFSET = "offset";
const char* CAL_SCALE = "scale";
const char* CAL_ZERO_RAW_ADC = "zero_raw_adc";
const char* CAL_SPAN_RAW_ADC = "span_raw_adc";
// Short NVS keys to stay within ESP32 NVS key length limits (<=15 chars)
const char* CAL_ZERO_PRESSURE_VALUE = "zpv"; // zero pressure value
const char* CAL_SPAN_PRESSURE_VALUE = "spv"; // span pressure value

// Legacy (long) key names used in older firmware. We keep these here so
// load routines can migrate existing values to the new short keys on first run.
const char* OLD_CAL_ZERO_PRESSURE_VALUE = "zero_pressure_value";
const char* OLD_CAL_SPAN_PRESSURE_VALUE = "span_pressure_value";