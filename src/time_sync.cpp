#include "time_sync.h"
#include "config.h"
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>

// Global variables defined here
RTC_DS3231 rtc;
bool rtcFound = false;
static bool timeSyncInitiated = false;
static unsigned long lastNtpAttempt = 0;
static unsigned long lastNtpSuccess = 0;
static bool pendingRtcSync = false; // true when we want to update RTC after NTP success
static Preferences preferences; // local Preferences instance for time persistence
const char* PREF_TIME_NS = "time";
const char* PREF_LAST_NTP_EPOCH = "last_ntp";
const char* PREF_LAST_NTP_ISO = "last_ntp_iso";
static bool rtcEnabled = true;

// NTP Servers Definition (defined once here)
const char* NTP_SERVERS[] = {"10.10.10.2", "pool.ntp.org", "time.nist.gov", "time.google.com"};

void syncNtp(bool updateRtcAfter) {
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Starting NTP synchronization...");
        const int numNtpServers = sizeof(NTP_SERVERS) / sizeof(NTP_SERVERS[0]);
        if (numNtpServers == 1) configTzTime(TIMEZONE, NTP_SERVERS[0]);
        else if (numNtpServers == 2) configTzTime(TIMEZONE, NTP_SERVERS[0], NTP_SERVERS[1]);
        else configTzTime(TIMEZONE, NTP_SERVERS[0], NTP_SERVERS[1], NTP_SERVERS[2]);

        timeSyncInitiated = true;
        lastNtpAttempt = millis();
        // Only request RTC update if RTC is enabled
        pendingRtcSync = (updateRtcAfter && rtcEnabled);
    }
}

time_t getLastNtpSuccessEpoch() {
    preferences.begin(PREF_TIME_NS, true);
    unsigned long val = preferences.getULong(PREF_LAST_NTP_EPOCH, 0);
    preferences.end();
    return (time_t)val;
}

String getLastNtpSuccessIso() {
    preferences.begin(PREF_TIME_NS, true);
    String s = preferences.getString(PREF_LAST_NTP_ISO, "");
    preferences.end();
    return s;
}

bool isRtcPresent() {
    return rtcFound;
}

time_t getRtcEpoch() {
    if (!rtcFound) return 0;
    DateTime now = rtc.now();
    struct tm tm_rtc;
    tm_rtc.tm_year = now.year() - 1900;
    tm_rtc.tm_mon = now.month() - 1;
    tm_rtc.tm_mday = now.day();
    tm_rtc.tm_hour = now.hour();
    tm_rtc.tm_min = now.minute();
    tm_rtc.tm_sec = now.second();
    return mktime(&tm_rtc);
}

bool isPendingRtcSync() {
    return pendingRtcSync;
}

void setRtcEnabled(bool enabled) {
    rtcEnabled = enabled;
    // persist the flag
    preferences.begin(PREF_TIME_NS, false);
    preferences.putInt(PREF_RTC_ENABLED, enabled ? 1 : 0);
    preferences.end();
}

bool getRtcEnabled() {
    // read persisted value if preferences available
    preferences.begin(PREF_TIME_NS, true);
    int v = preferences.getInt(PREF_RTC_ENABLED, DEFAULT_RTC_ENABLED);
    preferences.end();
    rtcEnabled = (v != 0);
    return rtcEnabled;
}

void checkNtpAndUpdateRtc() {
    if (timeSyncInitiated) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 1000)) { // 1s timeout
            if (timeinfo.tm_year > (2016 - 1900)) { // Check if time is valid
                Serial.println("NTP sync successful.");
                lastNtpSuccess = millis();
                // Persist last NTP epoch to preferences
                time_t nowEpoch = time(nullptr);
                // Persist epoch and ISO string of last successful NTP sync
                char isoBuf[32];
                struct tm *tm_now = localtime(&nowEpoch);
                if (tm_now) {
                    strftime(isoBuf, sizeof(isoBuf), "%Y-%m-%dT%H:%M:%SZ", tm_now);
                } else {
                    isoBuf[0] = '\0';
                }
                preferences.begin(PREF_TIME_NS, false);
                preferences.putULong(PREF_LAST_NTP_EPOCH, (unsigned long)nowEpoch);
                preferences.putString(PREF_LAST_NTP_ISO, String(isoBuf));
                preferences.end();
                timeSyncInitiated = false; // Sync is complete

                if (rtcFound && pendingRtcSync) {
                    Serial.println("Updating RTC from NTP time.");
                    rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
                    pendingRtcSync = false;
                }
            } else {
                Serial.println("NTP sync fetched invalid time, will retry.");
                timeSyncInitiated = false; // Reset to allow another sync attempt
            }
        } else {
            Serial.println("NTP sync in progress...");
        }
    }
}

void printCurrentTime() {
    if (!rtcFound) return;
    DateTime now = rtc.now();
    char buf[] = "YYYY/MM/DD hh:mm:ss";
    now.toString(buf);
    Serial.print("RTC Time: ");
    Serial.print(buf);
    Serial.print(" | Temp: ");
    Serial.print(rtc.getTemperature());
    Serial.println(" C");
}

void setupTimeSync() {
    // Load persisted RTC enabled flag
    preferences.begin(PREF_TIME_NS, true);
    int v = preferences.getInt(PREF_RTC_ENABLED, DEFAULT_RTC_ENABLED);
    preferences.end();
    rtcEnabled = (v != 0);

    if (!rtcEnabled) {
        Serial.println("RTC disabled by configuration; skipping RTC init.");
    }
    if (rtc.begin()) {
        Serial.println("RTC found.");
        rtcFound = true;
        if (rtc.lostPower()) {
            Serial.println("RTC lost power, will require NTP sync to set RTC.");
            // If RTC lost power, ensure we attempt NTP when WiFi is available
            pendingRtcSync = true;
        } else {
            // If RTC has valid time, seed system time from RTC now so logs and tasks have correct time before NTP
            DateTime now = rtc.now();
            struct tm tm_rtc;
            tm_rtc.tm_year = now.year() - 1900;
            tm_rtc.tm_mon = now.month() - 1;
            tm_rtc.tm_mday = now.day();
            tm_rtc.tm_hour = now.hour();
            tm_rtc.tm_min = now.minute();
            tm_rtc.tm_sec = now.second();
            time_t t = mktime(&tm_rtc);
            struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            Serial.println("System time seeded from RTC.");
            lastNtpSuccess = millis(); // consider RTC time as last known good time
        }
    } else {
        if (rtcEnabled) {
            Serial.println("RTC not found!");
        } else {
            // RTC disabled, not an error
            rtcFound = false;
        }
    }
    // Initial NTP sync will be called from main's setup after WiFi is connected
}

void loopTimeSync() {
    // Retry logic: attempt NTP if enough time passed since last success, and also respect retry interval after failed attempts
    unsigned long now = millis();
    bool timeToTry = false;

    if (lastNtpSuccess == 0) {
        // Never successfully synced; allow retry based on last attempt
        if (now - lastNtpAttempt > NTP_RETRY_INTERVAL) timeToTry = true;
    } else {
        // We have a successful sync before; schedule next full sync after NTP_SYNC_INTERVAL
        if (now - lastNtpSuccess > NTP_SYNC_INTERVAL && now - lastNtpAttempt > NTP_RETRY_INTERVAL) timeToTry = true;
    }

    if (timeToTry) syncNtp(pendingRtcSync);

    // Handle NTP sync completion and RTC update
    checkNtpAndUpdateRtc();
}
