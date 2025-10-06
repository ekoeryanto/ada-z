#include "wifi_manager_module.h"
#include "config.h" // For DEFAULT_SSID, DEFAULT_PASS, WM_AP_NAME
#include "time_sync.h" // For syncNtp()
#include "ota_updater.h" // For setupOtaUpdater()
#include "device_id.h" // getChipId()
#include <WiFi.h>
#include <esp_wifi_types.h>

static bool wifiHandlersRegistered = false;
static bool wifiReconnectPending = false;
static unsigned long nextReconnectAttemptMillis = 0;
static unsigned long reconnectDelayMs = 0;
static unsigned long lastReconnectAttemptMillis = 0;
static unsigned long lastWifiDisconnectMillis = 0;
static unsigned long lastWifiGotIpMillis = 0;
static uint32_t lastWifiDisconnectReason = 0;

static const unsigned long MIN_RECONNECT_INTERVAL_MS = 5UL * 1000UL;      // 5 seconds
static const unsigned long MAX_RECONNECT_INTERVAL_MS = 5UL * 60UL * 1000UL; // 5 minutes
static const unsigned long STORED_CRED_TIMEOUT_MS = 5000; // 5 seconds
static const unsigned long PREF_SSID_TIMEOUT_MS = 7000;   // 7 seconds per SSID

static bool tryConnectStored();
static bool tryConnectPreferred();

static void scheduleReconnect(unsigned long delayMs) {
    unsigned long now = millis();
    unsigned long actualDelay = delayMs;
    if (delayMs > 0 && delayMs < MIN_RECONNECT_INTERVAL_MS) {
        actualDelay = MIN_RECONNECT_INTERVAL_MS;
    }
    wifiReconnectPending = true;
    nextReconnectAttemptMillis = now + actualDelay;
    if (reconnectDelayMs < MIN_RECONNECT_INTERVAL_MS) {
        reconnectDelayMs = MIN_RECONNECT_INTERVAL_MS;
    }
    if (actualDelay > reconnectDelayMs) {
        reconnectDelayMs = actualDelay;
        if (reconnectDelayMs > MAX_RECONNECT_INTERVAL_MS) reconnectDelayMs = MAX_RECONNECT_INTERVAL_MS;
    }
}

static void registerWifiHandlers() {
    if (wifiHandlersRegistered) return;
    // Trigger NTP sync whenever the station gets an IP (reconnect)
    WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
        #if ENABLE_VERBOSE_LOGS
        Serial.println("WiFi event: STA_GOT_IP - triggering NTP sync and starting OTA");
        #endif
        // Trigger NTP sync immediately on IP acquisition
        syncNtp();
        // Ensure OTA updater is started after we have a valid IP (espota listens on TCP 3232)
        setupOtaUpdater();
        lastWifiGotIpMillis = millis();
        wifiReconnectPending = false;
        reconnectDelayMs = MIN_RECONNECT_INTERVAL_MS;
    }, ARDUINO_EVENT_WIFI_STA_GOT_IP);

    WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
        lastWifiDisconnectMillis = millis();
        lastWifiDisconnectReason = info.wifi_sta_disconnected.reason;
        #if ENABLE_VERBOSE_LOGS
        Serial.printf("WiFi event: STA_DISCONNECTED (reason=%u)\n", lastWifiDisconnectReason);
        #endif
        scheduleReconnect(MIN_RECONNECT_INTERVAL_MS);
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
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
        #if ENABLE_VERBOSE_LOGS
        Serial.print("Hostname set to: "); Serial.println(WiFi.getHostname());
        #endif
    }

    WiFi.setAutoReconnect(true);

    // If already connected to WiFi, use it.
    if (WiFi.status() == WL_CONNECTED) {
        #if ENABLE_VERBOSE_LOGS
        Serial.println("Already connected to WiFi — skipping portal.");
        #endif
        registerWifiHandlers();
        syncNtp();
        lastWifiGotIpMillis = millis();
        wifiReconnectPending = false;
        reconnectDelayMs = MIN_RECONNECT_INTERVAL_MS;
        return;
    }

    // Ensure we are in station mode and stop any background auto-connect attempts
    WiFi.mode(WIFI_STA);
    // Disconnect from any previous/auto attempts without erasing stored credentials
    WiFi.disconnect(true);
    delay(100);

    // Try to connect using stored credentials first (non-blocking quick attempt)
    #if ENABLE_VERBOSE_LOGS
    Serial.println("Not connected — attempting stored credentials and preferred SSIDs before opening portal");
    #endif
    registerWifiHandlers();

    // Quick attempt to connect using any stored credentials (if present)
    if (tryConnectStored() || tryConnectPreferred()) {
        syncNtp();
        lastWifiGotIpMillis = millis();
        wifiReconnectPending = false;
        reconnectDelayMs = MIN_RECONNECT_INTERVAL_MS;
        return;
    }

    #if ENABLE_VERBOSE_LOGS
    Serial.println("Preferred SSIDs failed — will retry later");
    #endif
    scheduleReconnect(MIN_RECONNECT_INTERVAL_MS);
}

void serviceWifiManager() {
    if (WiFi.status() == WL_CONNECTED) {
        wifiReconnectPending = false;
        return;
    }

    unsigned long now = millis();
    if (!wifiReconnectPending) {
        // If WiFi was never connected, keep trying periodically.
        scheduleReconnect(MIN_RECONNECT_INTERVAL_MS);
        return;
    }

    if ((long)(now - nextReconnectAttemptMillis) < 0) {
        return;
    }

    #if ENABLE_VERBOSE_LOGS
    Serial.printf("WiFi reconnect attempt (backoff %lu ms)\n", reconnectDelayMs);
    #endif
    WiFi.mode(WIFI_STA);
    if (tryConnectStored() || tryConnectPreferred()) {
        syncNtp();
        lastWifiGotIpMillis = millis();
        wifiReconnectPending = false;
        reconnectDelayMs = MIN_RECONNECT_INTERVAL_MS;
        return;
    }
    lastReconnectAttemptMillis = now;

    if (reconnectDelayMs < MAX_RECONNECT_INTERVAL_MS) {
        unsigned long nextDelay = reconnectDelayMs * 2;
        if (nextDelay > MAX_RECONNECT_INTERVAL_MS) nextDelay = MAX_RECONNECT_INTERVAL_MS;
        reconnectDelayMs = nextDelay;
    }

    nextReconnectAttemptMillis = now + reconnectDelayMs;
}

bool isWifiConnected() {
    return WiFi.status() == WL_CONNECTED;
}

unsigned long getLastWifiDisconnectMillis() {
    return lastWifiDisconnectMillis;
}

uint32_t getLastWifiDisconnectReason() {
    return lastWifiDisconnectReason;
}

unsigned long getLastWifiReconnectAttemptMillis() {
    return lastReconnectAttemptMillis;
}

unsigned long getNextWifiReconnectAttemptMillis() {
    return wifiReconnectPending ? nextReconnectAttemptMillis : 0UL;
}

unsigned long getCurrentWifiReconnectBackoffMs() {
    return reconnectDelayMs;
}

unsigned long getLastWifiGotIpMillis() {
    return lastWifiGotIpMillis;
}

const char* getWifiDisconnectReasonString(uint32_t reason) {
    switch (reason) {
        case WIFI_REASON_UNSPECIFIED: return "unspecified";
        case WIFI_REASON_AUTH_EXPIRE: return "auth expire";
        case WIFI_REASON_AUTH_LEAVE: return "auth leave";
        case WIFI_REASON_ASSOC_EXPIRE: return "assoc expire";
        case WIFI_REASON_ASSOC_TOOMANY: return "too many STA";
        case WIFI_REASON_NOT_AUTHED: return "not authed";
        case WIFI_REASON_NOT_ASSOCED: return "not assoc";
        case WIFI_REASON_ASSOC_LEAVE: return "assoc leave";
        case WIFI_REASON_ASSOC_NOT_AUTHED: return "assoc not authed";
        case WIFI_REASON_DISASSOC_PWRCAP_BAD: return "power cap bad";
        case WIFI_REASON_DISASSOC_SUPCHAN_BAD: return "sup channel bad";
        case WIFI_REASON_IE_INVALID: return "IE invalid";
        case WIFI_REASON_MIC_FAILURE: return "MIC failure";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4-way timeout";
        case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT: return "group key timeout";
        case WIFI_REASON_IE_IN_4WAY_DIFFERS: return "4-way IE differs";
        case WIFI_REASON_GROUP_CIPHER_INVALID: return "group cipher invalid";
        case WIFI_REASON_PAIRWISE_CIPHER_INVALID: return "pairwise cipher invalid";
        case WIFI_REASON_AKMP_INVALID: return "AKMP invalid";
        case WIFI_REASON_UNSUPP_RSN_IE_VERSION: return "RSN version unsupported";
        case WIFI_REASON_INVALID_RSN_IE_CAP: return "RSN cap invalid";
        case WIFI_REASON_802_1X_AUTH_FAILED: return "802.1X auth failed";
        case WIFI_REASON_CIPHER_SUITE_REJECTED: return "cipher rejected";
        case WIFI_REASON_BEACON_TIMEOUT: return "beacon timeout";
        case WIFI_REASON_NO_AP_FOUND: return "no AP found";
        case WIFI_REASON_AUTH_FAIL: return "auth fail";
        case WIFI_REASON_ASSOC_FAIL: return "assoc fail";
        case WIFI_REASON_HANDSHAKE_TIMEOUT: return "handshake timeout";
        default: return "unknown";
    }
}

static bool waitForConnection(unsigned long timeoutMs) {
    unsigned long start = millis();
    while ((millis() - start) < timeoutMs) {
        if (WiFi.status() == WL_CONNECTED) {
            return true;
        }
        delay(200);
    }
    return WiFi.status() == WL_CONNECTED;
}

static bool tryConnectStored() {
    #if ENABLE_VERBOSE_LOGS
    Serial.println("Attempting connection using stored credentials");
    #endif
    WiFi.reconnect();
    if (waitForConnection(STORED_CRED_TIMEOUT_MS)) {
        #if ENABLE_VERBOSE_LOGS
        Serial.println("Connected using stored credentials");
        #endif
        return true;
    }
    return false;
}

static bool tryConnectPreferred() {
    const char* const* ssids = PREFERRED_SSIDS;
    const char* const* passes = PREFERRED_PASSES;
    for (int i = 0; ssids && ssids[i] != nullptr; ++i) {
        const char* ssid = ssids[i];
        const char* pass = (passes && passes[i]) ? passes[i] : "";
        #if ENABLE_VERBOSE_LOGS
        Serial.printf("Trying preferred SSID: %s\n", ssid);
        #endif
        WiFi.begin(ssid, pass);
        if (waitForConnection(PREF_SSID_TIMEOUT_MS)) {
            #if ENABLE_VERBOSE_LOGS
            Serial.printf("Connected to preferred SSID: %s\n", ssid);
            #endif
            return true;
        }
        #if ENABLE_VERBOSE_LOGS
        Serial.printf("Timed out connecting to %s\n", ssid);
        #endif
    }
    return false;
}
