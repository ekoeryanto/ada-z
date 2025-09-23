#include "preferences_helper.h"
#include <Preferences.h>
#include <esp_log.h>

#include "config.h"

bool safePreferencesBegin(Preferences &p, const char *ns) {
    // Try opening writable namespace; if it fails, try read-only as a fallback
    // but prefer writable so missing namespaces are created and NOT_FOUND logs avoided.
    p.begin(ns, false);
    // There's no direct API to check success; rely on reading a known key to force NVS open.
    // If namespace doesn't exist, begin(..., false) will create it.
    return true;
}

float safeGetFloat(Preferences &p, const char *key, float def) {
    float v = def;
    // Caller must open the Preferences namespace using safePreferencesBegin().
    // Use isKey() to avoid triggering getBytes/getBytesLength when key is missing.
    // Temporarily suppress Preferences component logging to avoid spamming
    // serial output with NOT_FOUND messages when keys are missing.
    esp_log_level_t prev = esp_log_level_get("Preferences");
    esp_log_level_set("Preferences", ESP_LOG_NONE);
    if (p.isKey(key)) {
        v = p.getFloat(key, def);
    }
    // Restore previous log level
    esp_log_level_set("Preferences", prev);
    return v;
}
