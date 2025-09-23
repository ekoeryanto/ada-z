#ifndef VOLTAGE_PRESSURE_SENSOR_H
#define VOLTAGE_PRESSURE_SENSOR_H

#include <Arduino.h>

// Function to convert ADC value to voltage
float convert010V(int adc);

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

// Function to update voltage pressure sensor reading for a specific sensor (to be called in loop)
void updateVoltagePressureSensor(int pinIndex);

// Accessor to get the raw smoothed ADC value for a specific sensor
float getSmoothedADC(int pinIndex);
// Returns true if the pin has been saturated recently (consecutive full-scale reads)
bool isPinSaturated(int pinIndex);

#endif // VOLTAGE_PRESSURE_SENSOR_H

