#include <Arduino.h>
#include "storage_helpers.h"
#include "time_sync.h"

// Example integration: call these from your main sketch's setup() and loop().

void example_setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("Starting example storage integration...");

    // Initialize LittleFS and load NVS-stored config
    if (!initLittleFS()) {
        Serial.println("LittleFS failed to mount");
    }

    // Load WiFi creds (example)
    String ssid = loadFromNVS("wifi_ssid");
    String pass = loadFromNVS("wifi_pass");
    if (ssid.length()) {
        Serial.print("SSID: "); Serial.println(ssid);
        // Start WiFi if desired
    }

    // Time fallback
    bool rtc_ok = isRtcPresent() && !isRtcLostPower();
    if (!rtc_ok) {
        unsigned long last = loadULongFromNVS("last_valid_ts", 0);
        if (last) {
            Serial.print("Using fallback epoch from NVS: "); Serial.println(last);
            // set system time or use as reference
        }
    }

    // Apply persisted relay state
    bool relay = loadBoolFromNVS("relay_state", false);
    Serial.print("Relay restored: "); Serial.println(relay ? "ON" : "OFF");
    // digitalWrite(RELAY_PIN, relay ? HIGH : LOW);
}

void example_loop() {
    // Regularly log a fake sensor reading
    static unsigned long last = 0;
    if (millis() - last < 5000) return;
    last = millis();

    float val = analogRead(33) / 4095.0f * 10.0f;
    appendSensorLog("/sensor_log.jsonl", "AI1", getIsoTimestamp(), val);

    // Update relay state example
    static bool s = false;
    s = !s;
    saveBoolToNVS("relay_state", s);
    saveULongToNVS("last_valid_ts", (unsigned long)(isRtcPresent() ? getRtcEpoch() : time(nullptr)));
}

// Optionally expose a function to dump logs
void example_dump_logs() {
    Serial.println(loadFromLittleFS("/sensor_log.jsonl"));
}

// End of storage_integration_example.cpp
