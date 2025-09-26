// JSON helper utilities
#ifndef JSON_HELPER_H
#define JSON_HELPER_H

#include <ArduinoJson.h>
#if defined(ARDUINOJSON_VERSION_MAJOR)
#pragma GCC diagnostic push
// ArduinoJson v7+ uses JsonDocument for both static and dynamic allocation.
// The capacity is specified in the constructor.
#pragma GCC diagnostic pop
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
String buildJsonString(JsonDocument &doc);

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
