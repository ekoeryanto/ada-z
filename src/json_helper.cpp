#include "json_helper.h"

// Serialize a JsonDocument into an Arduino String
String buildJsonString(JsonDocument &doc) {
    String out;
    serializeJson(doc, out);
    return out;
}

DynamicJsonDocument makeStatusDoc(const char* status, const String &message, size_t capacity) {
    DynamicJsonDocument doc(capacity);
    setStatusMessage(doc, status, message);
    return doc;
}

DynamicJsonDocument makeErrorDoc(const String &message, size_t capacity) {
    DynamicJsonDocument doc(capacity);
    setStatusMessage(doc, "error", message);
    return doc;
}

DynamicJsonDocument makeSuccessDoc(const String &message, size_t capacity) {
    DynamicJsonDocument doc(capacity);
    setStatusMessage(doc, "success", message);
    return doc;
}

void setStatusMessage(JsonDocument &doc, const char* status, const String &message) {
    doc["status"] = status;
    if (message.length() > 0) {
        doc["message"] = message;
    } else if (!doc.containsKey("message")) {
        // Ensure message exists (empty string) for consistent schema
        doc["message"] = "";
    }
}
