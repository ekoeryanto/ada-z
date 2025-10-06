#include "web_api_handlers.h"
#include "web_api_common.h"
#include "time_sync.h"
#include <ArduinoJson.h>
#include <WiFi.h>

void registerSystemHandlers(AsyncWebServer *server) {
    if (!server) return;

    server->on("/api/time/sync", HTTP_POST, [](AsyncWebServerRequest *request) {
        syncNtp(isRtcPresent());
        sendJsonSuccess(request, 200, "NTP sync triggered");
    });

    server->on("/api/time/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["rtc_found"] = isRtcPresent() ? 1 : 0;
        doc["rtc_lost_power"] = isRtcLostPower() ? 1 : 0;
        time_t rtcEpoch = isRtcPresent() ? getRtcEpoch() : 0;
        doc["rtc_epoch"] = (unsigned long)rtcEpoch;
        if (isRtcPresent()) {
            doc["rtc_iso"] = formatIsoWithTz(rtcEpoch);
        } else {
            doc["rtc_iso"] = "";
        }
        time_t sysEpoch = time(nullptr);
        doc["system_epoch"] = (unsigned long)sysEpoch;
        doc["system_iso"] = formatIsoWithTz(sysEpoch);
        doc["last_ntp_epoch"] = (unsigned long)getLastNtpSuccessEpoch();
        doc["last_ntp_iso"] = getLastNtpSuccessIso();
        doc["pending_rtc_sync"] = isPendingRtcSync() ? 1 : 0;
    sendCorsJsonDoc(request, 200, doc);
    });

    server->on("/api/system", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        bool connected = WiFi.status() == WL_CONNECTED;
        doc["connected"] = connected ? 1 : 0;
        doc["ip"] = WiFi.localIP().toString();
        doc["hostname"] = WiFi.getHostname();
        doc["ssid"] = WiFi.SSID();
        doc["rssi"] = connected ? WiFi.RSSI() : 0;
        doc["uptime_ms"] = (unsigned long)millis();
        doc["rtc_found"] = isRtcPresent() ? 1 : 0;
        doc["rtc_enabled"] = getRtcEnabled() ? 1 : 0;
        doc["last_ntp_epoch"] = (unsigned long)getLastNtpSuccessEpoch();
        doc["last_ntp_iso"] = getLastNtpSuccessIso();
        doc["time_iso"] = getIsoTimestamp();
    sendCorsJsonDoc(request, 200, doc);
    });

    server->on("/api/tags", HTTP_GET, [](AsyncWebServerRequest *request) {
        String payload = loadTagMetadataJson();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            // fallback to raw string if stored payload is not valid JSON
            sendCorsJson(request, 200, "application/json", payload);
            return;
        }
        sendCorsJsonDoc(request, 200, doc);
    });
}
