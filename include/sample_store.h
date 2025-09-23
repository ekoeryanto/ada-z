#ifndef SAMPLE_STORE_H
#define SAMPLE_STORE_H

#include <Arduino.h>

// Initialize sample store for total sensors and per-sensor capacity
void initSampleStore(int totalSensors, int samplesPerSensor);

// Add a sample for a sensor (raw ADC, smoothed value, voltage-like value)
void addSample(int sensorIndex, int raw, float smoothed, float volt);

// Get averaged values for a sensor. Returns true if sample exists.
bool getAverages(int sensorIndex, float &avgRaw, float &avgSmoothed, float &avgVolt);

// Get number of samples currently stored for sensor
int getSampleCount(int sensorIndex);

// Deinitialize / free resources (optional)
void deinitSampleStore();

// Return configured per-sensor sample capacity
int getSampleCapacity();

// Resize per-sensor sample capacity at runtime (reinitializes buffers)
void resizeSampleStore(int samplesPerSensor);

#endif // SAMPLE_STORE_H
