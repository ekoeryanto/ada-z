#ifndef SENSORS_CONFIG_H
#define SENSORS_CONFIG_H

#include <Arduino.h>

// Initialize runtime sensor settings (called from main.setup)
void initSensorRuntimeSettings();

// Get number of sensors
int getConfiguredNumSensors();

// Get/Set per-sensor enabled flag
bool getSensorEnabled(int index);
void setSensorEnabled(int index, bool enabled);

// Get/Set per-sensor notification interval (ms)
unsigned long getSensorNotificationInterval(int index);
void setSensorNotificationInterval(int index, unsigned long interval);

// Persist per-sensor settings to Preferences (namespace: "sensors")
void persistSensorSettings();

#endif // SENSORS_CONFIG_H
