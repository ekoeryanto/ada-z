#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// This header provides small helper functions for NVS (Preferences) and LittleFS storage
// Designed for ESP32 + PlatformIO projects. Keep operations small and resilient.

// NVS namespace used by helpers. You can change this if your project uses another.
static const char* SH_PREF_NAMESPACE = "config";

// Initialize LittleFS. Returns true on success.
static bool initLittleFS() {
    if (!LittleFS.begin()) {
        Serial.println("LittleFS mount failed");
        return false;
    }
    return true;
}

// NVS helpers (wrap Preferences open/close per operation to be safe on concurrency)
static void saveStringToNVS(const char* key, const String &value) {
    Preferences p;
    p.begin(SH_PREF_NAMESPACE, false);
    p.putString(key, value);
    p.end();
}

static String loadStringFromNVS(const char* key, const String &def = String("")) {
    Preferences p;
    p.begin(SH_PREF_NAMESPACE, false);
    String v = p.getString(key, def);
    p.end();
    return v;
}

static void saveBoolToNVS(const char* key, bool v) {
    Preferences p; p.begin(SH_PREF_NAMESPACE, false); p.putBool(key, v); p.end();
}
static bool loadBoolFromNVS(const char* key, bool def = false) {
    Preferences p; p.begin(SH_PREF_NAMESPACE, false); bool v = p.getBool(key, def); p.end(); return v;
}

static void saveULongToNVS(const char* key, unsigned long v) {
    Preferences p; p.begin(SH_PREF_NAMESPACE, false); p.putULong(key, v); p.end();
}
static unsigned long loadULongFromNVS(const char* key, unsigned long def = 0) {
    Preferences p; p.begin(SH_PREF_NAMESPACE, false); unsigned long v = p.getULong(key, def); p.end(); return v;
}

static void saveFloatToNVS(const char* key, float v) {
    Preferences p; p.begin(SH_PREF_NAMESPACE, false); p.putFloat(key, v); p.end();
}
static float loadFloatFromNVS(const char* key, float def = 0.0f) {
    Preferences p; p.begin(SH_PREF_NAMESPACE, false); float v = p.getFloat(key, def); p.end(); return v;
}

// LittleFS helpers
static bool writeFileLittleFS(const char* path, const String &content) {
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    size_t written = f.print(content);
    f.close();
    return written == content.length();
}

static bool appendFileLittleFS(const char* path, const String &content) {
    File f = LittleFS.open(path, "a");
    if (!f) return false;
    size_t written = f.print(content);
    // Ensure newline
    if (written > 0) { f.print("\n"); }
    f.close();
    return written > 0;
}

static String readFileLittleFS(const char* path) {
    File f = LittleFS.open(path, "r");
    if (!f) return String("");
    String out;
    while (f.available()) out += (char)f.read();
    f.close();
    return out;
}

// Load JSON configuration from LittleFS into a provided JsonDocument.
// Returns true on success.
static bool loadConfigFromLittleFS(const char* path, JsonDocument &out) {
    String content = readFileLittleFS(path);
    if (content.length() == 0) return false;
    DeserializationError err = deserializeJson(out, content);
    return !err;
}

// High-level helpers: log sensor reading as JSON line (timestamp + sensor name + value)
// timestampIso should be e.g. output of getIsoTimestamp() or similar
static bool appendSensorLog(const char* path, const char* sensorId, const String &timestampIso, float value) {
    StaticJsonDocument<256> d;
    d["ts"] = timestampIso;
    d["sensor"] = sensorId;
    d["value"] = value;
    String s;
    serializeJson(d, s);
    return appendFileLittleFS(path, s);
}

// Convenience: save/restore minimal JSON blobs to NVS using a single key
// JSON is stored as string, useful for small config objects
static void saveJsonToNVS(const char* key, const JsonDocument &doc) {
    String s;
    serializeJson(doc, s);
    saveStringToNVS(key, s);
}

static bool loadJsonFromNVS(const char* key, JsonDocument &out) {
    String s = loadStringFromNVS(key, String(""));
    if (s.length() == 0) return false;
    DeserializationError err = deserializeJson(out, s);
    return !err;
}

// Small utility: ensure log directory exists (LittleFS has flat namespace but we mimic)
static void ensureLogPath(const char* path) {
    // LittleFS on ESP32 doesn't require mkdir for flat file path, but leaving placeholder
    (void)path;
}

// Simple public helpers matching requested names
static void saveToNVS(const char* key, const String &value) { saveStringToNVS(key, value); }
static String loadFromNVS(const char* key, const String &def = String("")) { return loadStringFromNVS(key, def); }
static void saveToLittleFS(const char* path, const String &content) { writeFileLittleFS(path, content); }
static String loadFromLittleFS(const char* path) { return readFileLittleFS(path); }

// End of storage_helpers.h
