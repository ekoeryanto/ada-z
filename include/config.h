// Single consolidated project configuration header
#ifndef CONFIG_H
#define CONFIG_H

#include <cstddef>

// WiFi defaults
#define DEFAULT_SSID "Perumda Tirta Patriot"
#define DEFAULT_PASS "1sampai8"
#define WM_AP_NAME   "adaAP"
#define WM_AP_PASS   "adaAPpass"

// Preferred SSID list + passwords
// Edit these arrays to customize preferred networks without changing code.
// The arrays are null-terminated; the WiFi code will iterate until a nullptr is found.
static const char* const PREFERRED_SSIDS[] = {
	"Perumda Tirta Patriot",
	"Pump Panel 1",
	"Pump Panel 2",
	nullptr
};

static const char* const PREFERRED_PASSES[] = {
	"1sampai8",
	"1sampai8",
	"1sampai8",
	nullptr
};

// mDNS
#define MDNS_HOSTNAME "ada-z"

// OTA
#define OTA_PORT     3232
#define OTA_PASSWORD "Mar9aMulya"

// Sensor timing defaults
#define SENSOR_READ_INTERVAL 5000 // ms (shortened for debugging)
#define EMA_ALPHA 0.05f
#define PRINT_TIME_INTERVAL 5000 // ms

// Logging verbosity
#ifndef ENABLE_VERBOSE_LOGS
#define ENABLE_VERBOSE_LOGS 0
#endif

#if ENABLE_VERBOSE_LOGS
#define LOG_VERBOSE(...) Serial.printf(__VA_ARGS__)
#define LOG_VERBOSE_LN(msg) Serial.println(msg)
#else
#define LOG_VERBOSE(...)
#define LOG_VERBOSE_LN(msg)
#endif

// Default per-sensor settings
#define DEFAULT_SENSOR_ENABLED true
#define DEFAULT_SENSOR_NOTIFICATION_INTERVAL (1 * 60 * 1000)

// NTP / timezone
extern const char* NTP_SERVERS[];
extern const size_t NTP_SERVER_COUNT;
#define TIMEZONE "GMT-7"
#define NTP_SYNC_INTERVAL (24 * 3600 * 1000)
#define NTP_RETRY_INTERVAL (5 * 60 * 1000)

// Preferences keys and defaults
#define PREF_RTC_ENABLED "rtc_enabled"
#define DEFAULT_RTC_ENABLED 1

#define PREF_SD_ENABLED "sd_enabled"
#define DEFAULT_SD_ENABLED 1

// Notification defaults
#define NOTIF_MODE_SERIAL  (1 << 0)
#define NOTIF_MODE_WEBHOOK (1 << 1)
#define DEFAULT_NOTIFICATION_MODE NOTIF_MODE_WEBHOOK

#define PAYLOAD_TYPE_COMPACT 0
#define PAYLOAD_TYPE_DETAILED 1
#define PAYLOAD_TYPE_RAW 2
#define DEFAULT_NOTIFICATION_PAYLOAD_TYPE PAYLOAD_TYPE_DETAILED

#define PREF_NOTIFICATION_MODE "notification_mode"
#define PREF_NOTIFICATION_PAYLOAD "notification_payload"

// Per-sensor preference keys
#define PREF_SENSOR_ENABLED_PREFIX "sensor_en_"
#define PREF_SENSOR_INTERVAL_PREFIX "sensor_iv_"

// Defaults for 4-20mA current pressure sensors (ADS1115)
// This project uses 0-10 bar transmitters by default. For water,
// 1 bar ≈ 10.19716213 meters (10197.16213 mm). Therefore full-scale
// depth for 10 bar is ~101971.6213 mm. `DEFAULT_DENSITY_WATER` is
// treated as specific gravity (1.0 = water). If you use different
// fluids, set density to the fluid's specific gravity.
#define DEFAULT_CURRENT_INIT_MA 4.00f
// Full-scale range in bars (default transmitter is 0-10 bar)
#define DEFAULT_RANGE_BAR 10.0f
// Full-scale range in millimetres of water column for DEFAULT_RANGE_BAR
#define DEFAULT_RANGE_MM 101971.6213f
#define DEFAULT_DENSITY_WATER 1.0f
#define DEFAULT_SHUNT_OHM 119.0f
#define DEFAULT_AMP_GAIN 2.0f

// HTTP notification endpoint (change to your URL)
#define HTTP_NOTIFICATION_URL "https://webhook.site/c68861fd-af04-4d83-a8af-e01c1df62f6b"
#define HTTP_NOTIFICATION_INTERVAL (1 * 60 * 1000)

// Simple header type for optional headers; actual arrays are defined in a .cpp if needed
struct HttpHeader { const char* key; const char* value; };
extern const HttpHeader HTTP_NOTIFICATION_HEADERS[];
extern const int NUM_HTTP_NOTIFICATION_HEADERS;

#endif // CONFIG_H
