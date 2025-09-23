// JSON helper utilities
#ifndef JSON_HELPER_H
#define JSON_HELPER_H

#include <ArduinoJson.h>

// Serialize a JsonDocument to String safely
String buildJsonString(JsonDocument &doc);

#endif // JSON_HELPER_H
