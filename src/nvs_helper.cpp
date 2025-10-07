#include "nvs_helper.h"

#include <Preferences.h>
#include <functional>

namespace NvsHelper {
namespace {
    bool withPreferences(const char* ns, bool readOnly, const std::function<bool(Preferences&)> &fn) {
        Preferences prefs;
        if (!prefs.begin(ns, readOnly)) {
            if (readOnly) {
                // Fallback to read-write to allow namespace creation without spamming errors
                if (!prefs.begin(ns, false)) {
                    return false;
                }
            } else {
                return false;
            }
        }
        bool ok = fn(prefs);
        prefs.end();
        return ok;
    }
}

bool writeString(const char* ns, const char* key, const String &value) {
    return withPreferences(ns, false, [&](Preferences &p) {
        p.putString(key, value);
        return true;
    });
}

String readString(const char* ns, const char* key, const String &def) {
    String result = def;
    withPreferences(ns, true, [&](Preferences &p) {
        if (p.isKey(key)) {
            result = p.getString(key, def);
        } else {
            result = def;
        }
        return true;
    });
    return result;
}

bool writeBool(const char* ns, const char* key, bool value) {
    return withPreferences(ns, false, [&](Preferences &p) {
        p.putBool(key, value);
        return true;
    });
}

bool readBool(const char* ns, const char* key, bool def) {
    bool result = def;
    withPreferences(ns, true, [&](Preferences &p) {
        if (p.isKey(key)) {
            result = p.getBool(key, def);
        } else {
            result = def;
        }
        return true;
    });
    return result;
}

bool writeUInt(const char* ns, const char* key, unsigned long value) {
    return withPreferences(ns, false, [&](Preferences &p) {
        p.putULong(key, value);
        return true;
    });
}

unsigned long readUInt(const char* ns, const char* key, unsigned long def) {
    unsigned long result = def;
    withPreferences(ns, true, [&](Preferences &p) {
        if (p.isKey(key)) {
            result = p.getULong(key, def);
        } else {
            result = def;
        }
        return true;
    });
    return result;
}

bool writeInt(const char* ns, const char* key, int value) {
    return withPreferences(ns, false, [&](Preferences &p) {
        p.putInt(key, value);
        return true;
    });
}

int readInt(const char* ns, const char* key, int def) {
    int result = def;
    withPreferences(ns, true, [&](Preferences &p) {
        if (p.isKey(key)) {
            result = p.getInt(key, def);
        } else {
            result = def;
        }
        return true;
    });
    return result;
}

bool writeFloat(const char* ns, const char* key, float value) {
    return withPreferences(ns, false, [&](Preferences &p) {
        p.putFloat(key, value);
        return true;
    });
}

float readFloat(const char* ns, const char* key, float def) {
    float result = def;
    withPreferences(ns, true, [&](Preferences &p) {
        if (p.isKey(key)) {
            result = p.getFloat(key, def);
        } else {
            result = def;
        }
        return true;
    });
    return result;
}

bool writeBytes(const char* ns, const char* key, const void* data, size_t len) {
    return withPreferences(ns, false, [&](Preferences &p) {
        size_t written = p.putBytes(key, data, len);
        return written == len;
    });
}

size_t bytesLength(const char* ns, const char* key) {
    size_t len = 0;
    withPreferences(ns, true, [&](Preferences &p) {
        if (p.isKey(key)) {
            len = p.getBytesLength(key);
        } else {
            len = 0;
        }
        return true;
    });
    return len;
}

bool readBytes(const char* ns, const char* key, void* outBuf, size_t len) {
    return withPreferences(ns, true, [&](Preferences &p) {
        if (!p.isKey(key)) {
            return false;
        }
        size_t have = p.getBytesLength(key);
        if (have != len) {
            return false;
        }
        p.getBytes(key, outBuf, len);
        return true;
    });
}

} // namespace NvsHelper
