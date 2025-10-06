#include "ota_updater.h"
#include "config.h" // For OTA_PORT, OTA_PASSWORD, MDNS_HOSTNAME, ENABLE_ARDUINO_OTA
#include "storage_helpers.h"

#if ENABLE_ARDUINO_OTA
#include <ArduinoOTA.h>

void setupOtaUpdater() {
    ArduinoOTA.setPort(OTA_PORT);
    // Use the WiFi hostname (may include chip suffix) so OTA uses the actual network name
    String otaHost = WiFi.getHostname();
    if (otaHost.length() == 0) otaHost = String(MDNS_HOSTNAME);
    ArduinoOTA.setHostname(otaHost.c_str());
    // Prefer an API key stored in NVS under namespace `config` key `api_key`.
    // If present and non-empty, use it as the ArduinoOTA password so both
    // HTTP /update and espota can share the same credential. Otherwise
    // fall back to the compile-time OTA_PASSWORD macro.
    String apiKey = loadStringFromNVSns("config", "api_key", String(""));
    if (apiKey.length() > 0) {
        ArduinoOTA.setPassword(apiKey.c_str());
        Serial.println("OTA: using api_key from NVS as ArduinoOTA password");
    } else {
        ArduinoOTA.setPassword(OTA_PASSWORD);
        Serial.println("OTA: using OTA_PASSWORD macro as ArduinoOTA password");
    }
    ArduinoOTA.onStart([]() { Serial.println("OTA: Start updating sketch"); });
    ArduinoOTA.onEnd([]() { Serial.println("OTA: End"); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("OTA: Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.begin();
    Serial.printf("OTA: started on port %d, mdns macro='%s' ota_hostname='%s'\n", OTA_PORT, MDNS_HOSTNAME, otaHost.c_str());
}

void handleOtaUpdate() {
    ArduinoOTA.handle();
}

#else // ENABLE_ARDUINO_OTA

void setupOtaUpdater() {
    // ArduinoOTA disabled to save flash; HTTP OTA remains available.
}

void handleOtaUpdate() {
    // no-op when ArduinoOTA support is disabled.
}

#endif
