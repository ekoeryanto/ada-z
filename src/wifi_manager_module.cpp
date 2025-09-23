#include "wifi_manager_module.h"
#include "config.h" // For DEFAULT_SSID, DEFAULT_PASS, WM_AP_NAME
#include "time_sync.h" // For syncNtp()
#include "ota_updater.h" // For setupOtaUpdater()
#include "device_id.h" // getChipId()
#include <WiFi.h>
#include <WiFiManager.h>

WiFiManager wm; // WiFiManager instance

static bool wifiHandlersRegistered = false;

static void registerWifiHandlers() {
    if (wifiHandlersRegistered) return;
    // Trigger NTP sync whenever the station gets an IP (reconnect)
    WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
        Serial.println("WiFi event: STA_GOT_IP - triggering NTP sync");
        syncNtp();
    }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
    wifiHandlersRegistered = true;
}

// Simpler WiFi connection strategy:
// 1. Try a short, non-blocking connection attempt to the configured SSID.
// 2. If it fails, bring up an AP and run blocking `wm.autoConnect()` so the user can configure WiFi.
// This is intentionally simple to avoid timing issues and background task complexity.
void setupAndConnectWiFi() {
    // Set a friendly hostname: base MDNS_HOSTNAME + '-' + last 6 hex of MAC
    {
        // Use efuse-based chip id (6-hex uppercase) as suffix so hostname matches notifications/API
        String desired = String(MDNS_HOSTNAME) + "-" + getChipId();
        WiFi.setHostname(desired.c_str());
        Serial.print("Hostname set to: "); Serial.println(WiFi.getHostname());
    }

    // If already connected to WiFi, use it.
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Already connected to WiFi — skipping portal.");
        registerWifiHandlers();
        syncNtp();
        return;
    }

    // Ensure we are in station mode and stop any background auto-connect attempts
    WiFi.mode(WIFI_STA);
    // Disconnect from any previous/auto attempts without erasing stored credentials
    WiFi.disconnect(true);
    delay(100);

    // Try to connect using stored credentials (WiFiManager will attempt saved creds first).
    // If that fails, WiFiManager will open a config AP and blocking portal for user setup.
    Serial.println("Not connected — attempting WiFiManager autoConnect (will use saved credentials or open portal)");
    // Note: OTA updater will be started from main once WiFi is available.
    // Allow a reasonable timeout for user to configure if portal opens
    wm.setConfigPortalTimeout(180); // 3 minutes
    registerWifiHandlers();
    if (wm.autoConnect(WM_AP_NAME, WM_AP_PASS)) {
        Serial.println("Connected via WiFiManager!");
        syncNtp();
    } else {
        Serial.println("WiFiManager portal timed out or failed. If no connection, device remains offline.");
    }
}
