// Preferences helper functions have been replaced by storage_helpers.h
// Provide compatibility wrappers to keep existing signatures.
#include "preferences_helper.h"
#include "storage_helpers.h"

bool safePreferencesBegin(void* /*p_unused*/, const char *ns) {
    // legacy compatibility: storage_helpers opens per-call; nothing to do here
    (void)ns;
    return true;
}

float safeGetFloat(void* /*p_unused*/, const char *key, float def) {
    // Read float from default namespace using centralized NVS helpers
    return NvsHelper::readFloat("config", key, def);
}
