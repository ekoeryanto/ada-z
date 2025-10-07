// Definitions for SSE debug event source
#include "web_api_common.h"
#include "web_api_json.h"
#include <AsyncEventSource.h>

// Define the global event source pointers
AsyncEventSource *eventSourceDebug = nullptr;
AsyncEventSource *eventSourceDebugAlias = nullptr;
AsyncEventSource *eventSourceSensors = nullptr;

namespace {

bool sensorsSnapshotDirty = false;

void sendSensorsSnapshotToClient(AsyncEventSourceClient *client) {
    if (!client) return;
    JsonDocument doc;
    buildSensorsReadingsJson(doc);
    String payload;
    serializeJson(doc, payload);
    client->send(payload.c_str(), "sensors", millis());
}

void broadcastSensorsSnapshot() {
    if (!eventSourceSensors) return;
    JsonDocument doc;
    buildSensorsReadingsJson(doc);
    String payload;
    serializeJson(doc, payload);
    eventSourceSensors->send(payload.c_str(), "sensors", millis());
}

} // namespace

void pushSseDebugMessage(const char *event, const String &payload) {
    if (eventSourceDebug) {
        eventSourceDebug->send(payload.c_str(), event, millis());
    }
    if (eventSourceDebugAlias) {
        eventSourceDebugAlias->send(payload.c_str(), event, millis());
    }
}

void pushSensorsSnapshotEvent() {
    broadcastSensorsSnapshot();
}

void flagSensorsSnapshotUpdate() {
    sensorsSnapshotDirty = true;
}

void serviceSensorsSnapshotUpdates() {
    if (!sensorsSnapshotDirty) return;
    sensorsSnapshotDirty = false;
    broadcastSensorsSnapshot();
}

// Forward declaration for registerSensorHandlers to hook new SSE stream
void ensureSensorSseRegistered(AsyncWebServer *server) {
    if (!server) return;
    if (!eventSourceSensors) {
        eventSourceSensors = new AsyncEventSource("/api/sse/sensors");
        eventSourceSensors->onConnect([](AsyncEventSourceClient *client) {
            sendSensorsSnapshotToClient(client);
        });
        server->addHandler(eventSourceSensors);
    }
}
