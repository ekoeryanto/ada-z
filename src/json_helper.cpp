#include "json_helper.h"

String buildJsonString(JsonDocument &doc) {
    String out;
    serializeJson(doc, out);
    return out;
}
