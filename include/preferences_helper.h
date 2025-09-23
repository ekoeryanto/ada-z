// Simple helper to centralize safe Preferences::begin behavior
#ifndef PREFERENCES_HELPER_H
#define PREFERENCES_HELPER_H

#include <Preferences.h>

// Opens the given `Preferences` instance with `ns` namespace in writable mode if possible.
// Returns true if begin succeeded, false otherwise.
bool safePreferencesBegin(Preferences &p, const char *ns);

// Read a float from Preferences while temporarily suppressing the "Preferences"
// component log output so missing-key errors don't spam the serial log.
float safeGetFloat(Preferences &p, const char *key, float def);

#endif // PREFERENCES_HELPER_H
