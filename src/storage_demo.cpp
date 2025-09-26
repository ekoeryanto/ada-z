#include "storage_helpers.h"
#include "time_sync.h" // Assumes getIsoTimestamp(), isRtcPresent(), getRtcEpoch()
#include <WiFi.h>

// Keys used in NVS
static const char* KEY_WIFI_SSID = "wifi_ssid";
static const char* KEY_WIFI_PASS = "wifi_pass";
static const char* KEY_DEVICE_ID = "device_id";
static const char* KEY_API_KEY = "api_key";
static const char* KEY_RELAY_STATE = "relay_state"; // store as bool
static const char* KEY_LAST_VALID_TS = "last_valid_ts"; // store as unsigned long epoch
static const char* KEY_CAL_FACTOR_PREFIX = "cal_"; // prefix for calibration factors per sensor

// LittleFS path for sensor logs
static const char* SENSOR_LOG_PATH = "/sensor_log.jsonl"; // JSON lines: {ts,sensor,value}

// Example: call at boot from setup()
void storage_init_and_boot_flow() {
    Serial.println("Initializing storage and loading config...");
    // Mount LittleFS
    if (!initLittleFS()) {
        Serial.println("LittleFS init failed - continuing with limited functionality");
    }

    // Load WiFi credentials from NVS
    String ssid = loadStringFromNVS(KEY_WIFI_SSID, String(""));
    String pass = loadStringFromNVS(KEY_WIFI_PASS, String(""));
    if (ssid.length() > 0) {
        Serial.print("Loaded WiFi SSID from NVS: "); Serial.println(ssid);
        // Optionally attempt to connect
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), pass.c_str());
        unsigned long start = millis();
        while (millis() - start < 5000 && WiFi.status() != WL_CONNECTED) delay(100);
        if (WiFi.status() == WL_CONNECTED) Serial.println("Connected to WiFi via saved credentials");
        else Serial.println("Could not connect with saved WiFi credentials (may be wrong or out of range)");
    } else {
        Serial.println("No WiFi creds in NVS");
    }

    // Load device id or other small config
    String devId = loadStringFromNVS(KEY_DEVICE_ID, String("unknown"));
    Serial.print("Device ID: "); Serial.println(devId);

    // Time handling: ensure RTC is valid else fallback to last saved timestamp
    bool rtc_ok = isRtcPresent() && !isRtcLostPower();
    if (rtc_ok) {
        Serial.println("RTC present and looks good");
    } else {
        unsigned long fallback = loadULongFromNVS(KEY_LAST_VALID_TS, 0);
        if (fallback > 0) {
            // If RTC not present or lost power, use this as current time reference
            Serial.print("RTC invalid - using last_valid_ts from NVS: "); Serial.println(fallback);
            // If you have a software-only time store, you'd set system time here; example omitted.
        } else {
            Serial.println("No last_valid_ts available in NVS - time may be unknown until NTP sync");
        }
    }

    // Restore last relay state
    bool relay_on = loadBoolFromNVS(KEY_RELAY_STATE, false);
    Serial.print("Restored relay state: "); Serial.println(relay_on ? "ON" : "OFF");
    // TODO: apply to your relay pin with digitalWrite
}

// Example: call whenever relay state changes to persist it and update last valid timestamp
void persist_relay_state_and_timestamp(bool relayOn) {
    saveBoolToNVS(KEY_RELAY_STATE, relayOn);
    // Save last valid timestamp (prefer RTC epoch if available)
    unsigned long nowEpoch = 0;
    if (isRtcPresent() && !isRtcLostPower()) {
        nowEpoch = (unsigned long)getRtcEpoch();
    } else {
        nowEpoch = (unsigned long)time(nullptr); // best-effort system time
    }
    saveULongToNVS(KEY_LAST_VALID_TS, nowEpoch);
}

// Example: log a sensor reading with timestamp into LittleFS
void log_sensor_reading(const char* sensorId, float value) {
    String iso = getIsoTimestamp();
    if (iso.length() == 0) {
        // fallback to epoch stored in NVS
        unsigned long fallback = loadULongFromNVS(KEY_LAST_VALID_TS, 0);
        if (fallback > 0) {
            iso = String(fallback);
        } else {
            iso = String(millis()); // last resort
        }
    }
    bool ok = appendSensorLog(SENSOR_LOG_PATH, sensorId, iso, value);
    if (!ok) {
        Serial.println("Failed to append sensor log to LittleFS");
    }
}

// Example usage in loop: periodically read a sensor and log
void storage_demo_loop_iteration() {
    static unsigned long lastMs = 0;
    if (millis() - lastMs < 5000) return; // every 5s
    lastMs = millis();

    // Example read: replace with actual sensor read
    float fake = analogRead(33) / 4095.0f * 10.0f; // sample
    log_sensor_reading("AI1", fake);
    // Optionally persist last timestamp so we have a fallback
    persist_relay_state_and_timestamp(loadBoolFromNVS(KEY_RELAY_STATE, false));
}

// Provide small helper to read and print log file (for diagnostic endpoints)
void print_sensor_log_to_serial() {
    String content = readFileLittleFS(SENSOR_LOG_PATH);
    if (content.length() == 0) {
        Serial.println("No sensor log available");
        return;
    }
    Serial.println("-- sensor log --");
    Serial.println(content);
    Serial.println("-- end log --");
}

// End of storage_demo.cpp
