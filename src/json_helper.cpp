#include "json_helper.h"

String buildJsonString(JsonDocDyn &doc) {
    String out;
    serializeJson(doc, out);
    return out;
}
