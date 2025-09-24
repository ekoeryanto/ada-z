// JSON helper utilities
#ifndef JSON_HELPER_H
#define JSON_HELPER_H

#include <ArduinoJson.h>
#if defined(ARDUINOJSON_VERSION_MAJOR)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
// Alias used across the project: DynamicJsonDocument for heap-allocated documents.
// Use DynamicJsonDocument with explicit capacity to avoid ambiguous construction.
using JsonDocDyn = DynamicJsonDocument;
#pragma GCC diagnostic pop
#endif

// Serialize a JsonDocDyn (DynamicJsonDocument) to String safely
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
String buildJsonString(JsonDocDyn &doc);
#pragma GCC diagnostic pop

#endif // JSON_HELPER_H
