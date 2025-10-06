#include "json_helper.h"

// Serialize a JsonDocument into an Arduino String
String buildJsonString(JsonDocument &doc) {
    String out;
    serializeJson(doc, out);
    return out;
}

JsonDocument makeStatusDoc(const char* status, const String &message, size_t capacity) {
    JsonDocument doc;
    setStatusMessage(doc, status, message);
    return doc;
}

JsonDocument makeErrorDoc(const String &message, size_t capacity) {
    JsonDocument doc;
    setStatusMessage(doc, "error", message);
    return doc;
}

JsonDocument makeSuccessDoc(const String &message, size_t capacity) {
    JsonDocument doc;
    setStatusMessage(doc, "success", message);
    return doc;
}

void setStatusMessage(JsonDocument &doc, const char* status, const String &message) {
    doc["status"] = status;
    if (message.length() > 0) {
        doc["message"] = message;
    } else if (doc["message"].isNull()) {
        // Ensure message exists (empty string) for consistent schema
        doc["message"] = "";
    }
}
