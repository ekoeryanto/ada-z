#ifndef CALIBRATION_KEYS_H
#define CALIBRATION_KEYS_H

// Calibration Data Keys (declared as extern)
extern const char* CAL_NAMESPACE;
extern const char* CAL_OFFSET;
extern const char* CAL_SCALE;
extern const char* CAL_ZERO_RAW_ADC;
extern const char* CAL_SPAN_RAW_ADC;
extern const char* CAL_ZERO_PRESSURE_VALUE;
extern const char* CAL_SPAN_PRESSURE_VALUE;
// Legacy (long) key names kept for migration support
extern const char* OLD_CAL_ZERO_PRESSURE_VALUE;
extern const char* OLD_CAL_SPAN_PRESSURE_VALUE;

#endif // CALIBRATION_KEYS_H
