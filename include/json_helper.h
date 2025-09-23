// JSON helper utilities
#ifndef JSON_HELPER_H
#define JSON_HELPER_H

#include <ArduinoJson.h>
#if defined(ARDUINOJSON_VERSION_MAJOR)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
using JsonDocDyn = DynamicJsonDocument;
#pragma GCC diagnostic pop
#endif

// Serialize a JsonDocDyn (DynamicJsonDocument) to String safely
String buildJsonString(JsonDocDyn &doc);

#endif // JSON_HELPER_H
