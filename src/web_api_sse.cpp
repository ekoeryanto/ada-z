// Definitions for SSE debug event source
#include "web_api_common.h"
#include <AsyncEventSource.h>

// Define the global event source pointers
AsyncEventSource *eventSourceDebug = nullptr;
AsyncEventSource *eventSourceDebugAlias = nullptr;

void pushSseDebugMessage(const char *event, const String &payload) {
    if (eventSourceDebug) {
        eventSourceDebug->send(payload.c_str(), event, millis());
    }
    if (eventSourceDebugAlias) {
        eventSourceDebugAlias->send(payload.c_str(), event, millis());
    }
}
