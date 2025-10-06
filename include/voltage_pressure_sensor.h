#ifndef VOLTAGE_PRESSURE_SENSOR_H
#define VOLTAGE_PRESSURE_SENSOR_H

#include <Arduino.h>

// Function to convert ADC value to voltage
float convert010V(int adc);
// Per-pin aware conversion helper
float convert010VForPin(int adc, int pinIndex);

// Convert ADC raw reading to millivolts using esp_adc_cal
int adcRawToMv(int raw);

// Function to get smoothed voltage pressure sensor reading for a specific sensor
float getSmoothedVoltagePressure(int pinIndex);

// Save calibration values for a specific sensor index (persists to Preferences)
void saveCalibrationForPin(int pinIndex, float zeroRawAdc, float spanRawAdc, float zeroPressureValue, float spanPressureValue);

// Get the current calibration for a specific sensor index
struct SensorCalibration getCalibrationForPin(int pinIndex);

// Return the GPIO pin number for a given sensor index
int getVoltageSensorPin(int pinIndex);

// Find sensor index by GPIO pin number; returns -1 if not found
int findVoltageSensorIndexByPin(int pinNumber);

// Return number of configured voltage sensors
int getNumVoltageSensors();

// Function to initialize voltage pressure sensor
void setupVoltagePressureSensor();

// Initialize ADC characterization (esp_adc_cal) for accurate mV conversion
void initAdcCalibration();

// Return the measured baseline mV for raw==0 (subtracted from conversions)
int getAdcZeroBaselineMv();

// Re-measure the ADC zero baseline at runtime and return the new baseline (mV)
int rebaselineAdcZero();

// Per-pin baseline persistence: get/set measured baseline for a given sensor index
int getAdcZeroBaselineForPin(int pinIndex);
void saveAdcZeroBaselineForPin(int pinIndex, int baselineMv);

// Per-pin divider persistence (divider_mv maps 10V input -> mv seen by ADC)
float getDividerMvForPin(int pinIndex);
void saveDividerMvForPin(int pinIndex, float dividerMv);

// Function to update voltage pressure sensor reading for a specific sensor (to be called in loop)
void updateVoltagePressureSensor(int pinIndex);

// Accessor to get the raw smoothed ADC value for a specific sensor
float getSmoothedADC(int pinIndex);
// Returns true if the pin has been saturated recently (consecutive full-scale reads)
bool isPinSaturated(int pinIndex);

// Runtime-configurable ADC smoothing parameters
int getAdcNumSamples();
void setAdcNumSamples(int n);


#endif // VOLTAGE_PRESSURE_SENSOR_H

