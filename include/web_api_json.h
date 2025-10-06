#ifndef WEB_API_JSON_H
#define WEB_API_JSON_H

#include <ArduinoJson.h>

// Build JSON document for sensors/readings into the provided JsonDocument.
// The function will populate timestamp, network, rtu, tags array and tags_total.
void buildSensorsReadingsJson(JsonDocument &doc);

// Build JSON document representing calibration for a single pin index.
void buildCalibrationJsonForPin(JsonDocument &doc, int pinIndex);

#endif // WEB_API_JSON_H
