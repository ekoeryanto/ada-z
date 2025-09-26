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

// Round a float to N decimal places (default 2) and return as float
inline float roundToDecimals(float v, int decimals = 2) {
	float mul = powf(10.0f, (float)decimals);
	return roundf(v * mul) / mul;
}

// Return a string with exactly N decimals for consistent JSON formatting
inline String formatFloatFixed(float v, int decimals = 2) {
	char buf[32];
	snprintf(buf, sizeof(buf), "%.*f", decimals, v);
	return String(buf);
}
#pragma GCC diagnostic pop

#endif // JSON_HELPER_H
