// Simple helper to centralize safe Preferences::begin behavior
#ifndef PREFERENCES_HELPER_H
#define PREFERENCES_HELPER_H

#include "storage_helpers.h"

// Legacy compatibility wrappers. The implementation uses storage_helpers which
// opens NVS per-operation; these functions keep existing call sites working.
bool safePreferencesBegin(void* p_unused, const char *ns);

float safeGetFloat(void* p_unused, const char *key, float def);

#endif // PREFERENCES_HELPER_H
