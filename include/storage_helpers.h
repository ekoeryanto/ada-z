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

// Namespace-aware variants (specify NVS namespace explicitly)
static void saveStringToNVSns(const char* ns, const char* key, const String &value) {
    Preferences p;
    p.begin(ns, false);
    p.putString(key, value);
    p.end();
}

static String loadStringFromNVSns(const char* ns, const char* key, const String &def = String("")) {
    Preferences p;
    p.begin(ns, false);
    String v = p.getString(key, def);
    p.end();
    return v;
}

// Namespace-aware variants for basic types
static void saveBoolToNVSns(const char* ns, const char* key, bool v) { Preferences p; p.begin(ns, false); p.putBool(key, v); p.end(); }
static bool loadBoolFromNVSns(const char* ns, const char* key, bool def = false) { Preferences p; p.begin(ns, false); bool v = p.getBool(key, def); p.end(); return v; }

static void saveULongToNVSns(const char* ns, const char* key, unsigned long v) { Preferences p; p.begin(ns, false); p.putULong(key, v); p.end(); }
static unsigned long loadULongFromNVSns(const char* ns, const char* key, unsigned long def = 0) { Preferences p; p.begin(ns, false); unsigned long v = p.getULong(key, def); p.end(); return v; }

static void saveFloatToNVSns(const char* ns, const char* key, float v) { Preferences p; p.begin(ns, false); p.putFloat(key, v); p.end(); }
static float loadFloatFromNVSns(const char* ns, const char* key, float def = 0.0f) { Preferences p; p.begin(ns, false); float v = p.getFloat(key, def); p.end(); return v; }

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

// Int helpers (Preferences uses 32-bit ints)
static void saveIntToNVS(const char* key, int v) { Preferences p; p.begin(SH_PREF_NAMESPACE, false); p.putInt(key, v); p.end(); }
static int loadIntFromNVS(const char* key, int def = 0) { Preferences p; p.begin(SH_PREF_NAMESPACE, false); int v = p.getInt(key, def); p.end(); return v; }

// Namespace-aware int helpers
static void saveIntToNVSns(const char* ns, const char* key, int v) { Preferences p; p.begin(ns, false); p.putInt(key, v); p.end(); }
static int loadIntFromNVSns(const char* ns, const char* key, int def = 0) { Preferences p; p.begin(ns, false); int v = p.getInt(key, def); p.end(); return v; }

// Byte array helpers for storing binary blobs
static bool saveBytesToNVSns(const char* ns, const char* key, const void* data, size_t len) {
    Preferences p; p.begin(ns, false);
    size_t written = p.putBytes(key, data, len);
    p.end();
    return written == len;
}

static size_t getBytesLengthFromNVSns(const char* ns, const char* key) {
    Preferences p; p.begin(ns, false);
    size_t len = p.getBytesLength(key);
    p.end();
    return len;
}

static bool loadBytesFromNVSns(const char* ns, const char* key, void* outBuf, size_t len) {
    Preferences p; p.begin(ns, false);
    size_t have = p.getBytesLength(key);
    if (have != len) { p.end(); return false; }
    p.getBytes(key, outBuf, len);
    p.end();
    return true;
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
    JsonDocument d;
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
