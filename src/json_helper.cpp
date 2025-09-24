#include "json_helper.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
String buildJsonString(JsonDocDyn &doc) {
    String out;
    serializeJson(doc, out);
    return out;
}
#pragma GCC diagnostic pop
