#include "wifi_manager_module.h"
#include "config.h" // For DEFAULT_SSID, DEFAULT_PASS, WM_AP_NAME
#include "time_sync.h" // For syncNtp()
#include "ota_updater.h" // For setupOtaUpdater()
#include "device_id.h" // getChipId()
#include <WiFi.h>
#include <WiFiManager.h>

WiFiManager wm; // Define WiFiManager object here

void setupAndConnectWiFi() {
    // Set a friendly hostname: base MDNS_HOSTNAME + '-' + last 6 hex of MAC
    {
        // Use efuse-based chip id (6-hex uppercase) as suffix so hostname matches notifications/API
        String desired = String(MDNS_HOSTNAME) + "-" + getChipId();
        WiFi.setHostname(desired.c_str());
        Serial.print("Hostname set to: "); Serial.println(WiFi.getHostname());
    }

    // If we're already connected to the default SSID, nothing to do
    if (WiFi.status() == WL_CONNECTED) {
        String cur = WiFi.SSID();
        if (cur == String(DEFAULT_SSID)) {
            Serial.print("Already connected to configured SSID '"); Serial.print(DEFAULT_SSID); Serial.println("' — skipping auto-connect.");
            syncNtp();
            return;
        }
    }

    // First, scan for the default SSID to avoid waiting if it's not in range
    WiFi.mode(WIFI_STA);
    Serial.println("Scanning for nearby SSIDs...");
    int n = WiFi.scanNetworks();
    Serial.printf("Found %d networks:\n", n);
    bool found = false;
    for (int i = 0; i < n; ++i) {
        String ssid = WiFi.SSID(i);
        int rssi = WiFi.RSSI(i);
        bool open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
        Serial.printf("  %d: %s (RSSI=%d) %s\n", i+1, ssid.c_str(), rssi, open ? "[open]" : "");
        if (ssid == String(DEFAULT_SSID)) { found = true; }
    }
    WiFi.scanDelete();

    if (found) {
        Serial.print("Configured SSID '"); Serial.print(DEFAULT_SSID); Serial.println("' found — attempting to connect...");
    } else {
        Serial.print("Configured SSID '"); Serial.print(DEFAULT_SSID); Serial.println("' not seen — will attempt a short connection attempt anyway.");
    }

    // Try a short connection attempt to the configured SSID regardless of scan result
    WiFi.begin(DEFAULT_SSID, DEFAULT_PASS);
    Serial.print("Connecting to WiFi");
    int attempts = 0;
    const int maxAttempts = 10; // try ~5 seconds
    while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Connected to WiFi!");
        syncNtp(); // Initiate first NTP sync from time_sync module
        return;
    }
    Serial.println("Short connection attempt failed — falling back to config portal.");

    // Start a temporary AP so OTA is available during the config portal
    Serial.println("Starting temporary AP for config portal and enabling OTA...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WM_AP_NAME, WM_FALLBACK_PASS);
    Serial.print("Temp AP started: "); Serial.println(WM_AP_NAME);
    // Start OTA so the device can be updated while in AP/config portal
    setupOtaUpdater();

    // Start WiFiManager config portal
    wm.setConfigPortalTimeout(180);
    if (wm.autoConnect(WM_AP_NAME)) {
        Serial.println("Connected via WiFiManager!");
        syncNtp(); // Initiate first NTP sync from time_sync module
    } else {
        Serial.println("Failed to connect and hit timeout. Keeping fallback AP active.");
        // Ensure fallback AP is active (softAP already started) and OTA remains running
        Serial.print("Fallback AP running: "); Serial.println(WM_AP_NAME);
    }
}
