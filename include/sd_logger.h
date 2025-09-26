#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include <Arduino.h>
#include <SD.h>

// Function prototypes for SD card logging
void setupSdLogger();
void logSensorDataToSd(String data);

// Error log helpers
void logErrorToSd(const String &msg);
String readErrorLog(int maxLines = -1); // maxLines=-1 => return whole file
void clearErrorLog();

// Enable/disable SD logging at runtime
void setSdEnabled(bool enabled);
bool getSdEnabled();

// Extern declaration for global variable
extern bool sdCardFound;

#endif // SD_LOGGER_H
#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include <Arduino.h>
#include <SD.h>

// Function prototypes for SD card logging
void setupSdLogger();
void logSensorDataToSd(String data);

// Enable/disable SD logging at runtime
void setSdEnabled(bool enabled);
bool getSdEnabled();

// Extern declaration for global variable
extern bool sdCardFound;

#endif // SD_LOGGER_H
