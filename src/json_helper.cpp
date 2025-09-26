#include "json_helper.h"

// The type needs to be JsonDocument for v7+
String buildJsonString(JsonDocument &doc) {
    String out;
    serializeJson(doc, out);
    return out;
}
