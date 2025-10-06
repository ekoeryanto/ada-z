#pragma once

#include <Arduino.h>

namespace NvsHelper {
    constexpr const char* kDefaultNamespace = "config";

    bool writeString(const char* ns, const char* key, const String &value);
    String readString(const char* ns, const char* key, const String &def = String(""));

    bool writeBool(const char* ns, const char* key, bool value);
    bool readBool(const char* ns, const char* key, bool def = false);

    bool writeUInt(const char* ns, const char* key, unsigned long value);
    unsigned long readUInt(const char* ns, const char* key, unsigned long def = 0);

    bool writeInt(const char* ns, const char* key, int value);
    int readInt(const char* ns, const char* key, int def = 0);

    bool writeFloat(const char* ns, const char* key, float value);
    float readFloat(const char* ns, const char* key, float def = 0.0f);

    bool writeBytes(const char* ns, const char* key, const void* data, size_t len);
    size_t bytesLength(const char* ns, const char* key);
    bool readBytes(const char* ns, const char* key, void* outBuf, size_t len);

    // Default-namespace convenience wrappers
    inline bool writeString(const char* key, const String &value) {
        return writeString(kDefaultNamespace, key, value);
    }
    inline String readString(const char* key, const String &def = String("")) {
        return readString(kDefaultNamespace, key, def);
    }
    inline bool writeBool(const char* key, bool value) {
        return writeBool(kDefaultNamespace, key, value);
    }
    inline bool readBool(const char* key, bool def = false) {
        return readBool(kDefaultNamespace, key, def);
    }
    inline bool writeUInt(const char* key, unsigned long value) {
        return writeUInt(kDefaultNamespace, key, value);
    }
    inline unsigned long readUInt(const char* key, unsigned long def = 0) {
        return readUInt(kDefaultNamespace, key, def);
    }
    inline bool writeInt(const char* key, int value) {
        return writeInt(kDefaultNamespace, key, value);
    }
    inline int readInt(const char* key, int def = 0) {
        return readInt(kDefaultNamespace, key, def);
    }
    inline bool writeFloat(const char* key, float value) {
        return writeFloat(kDefaultNamespace, key, value);
    }
    inline float readFloat(const char* key, float def = 0.0f) {
        return readFloat(kDefaultNamespace, key, def);
    }
    inline bool writeBytes(const char* key, const void* data, size_t len) {
        return writeBytes(kDefaultNamespace, key, data, len);
    }
    inline size_t bytesLength(const char* key) {
        return bytesLength(kDefaultNamespace, key);
    }
    inline bool readBytes(const char* key, void* outBuf, size_t len) {
        return readBytes(kDefaultNamespace, key, outBuf, len);
    }
}

