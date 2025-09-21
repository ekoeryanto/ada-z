#include "device_id.h"
#include <WiFi.h>

String getChipId() {
    uint64_t mac = ESP.getEfuseMac(); // 48-bit MAC in efuse
    // take lower 3 bytes for a compact ID (matches examples like CBC5A8)
    uint32_t lower = (uint32_t)(mac & 0xFFFFFF);
    char buf[8];
    sprintf(buf, "%06X", lower);
    return String(buf);
}
