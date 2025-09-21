#ifndef HTTP_NOTIFIER_H
#define HTTP_NOTIFIER_H

#include <Arduino.h>

// Function to send HTTP notification for a specific sensor index
void sendHttpNotification(int sensorIndex, int rawADC, float smoothedADC, float voltage);

// Function to send a single HTTP POST with an array of sensor readings
// The arrays must have length == numSensors
void sendHttpNotificationBatch(int numSensors, int sensorIndices[], int rawADC[], float smoothedADC[]);

// Configuration API for notifications
void setNotificationMode(uint8_t modeMask);
uint8_t getNotificationMode();

void setNotificationPayloadType(uint8_t payloadType);
uint8_t getNotificationPayloadType();

// Helper to route a single sensor notification according to current mode
void routeSensorNotification(int sensorIndex, int rawADC, float smoothedADC, float voltage);

#endif // HTTP_NOTIFIER_H
