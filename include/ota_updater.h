#ifndef OTA_UPDATER_H
#define OTA_UPDATER_H

#include <Arduino.h>

// Function to set up OTA updates
void setupOtaUpdater();

// Function to handle OTA updates (call in loop)
void handleOtaUpdate();

#endif // OTA_UPDATER_H
