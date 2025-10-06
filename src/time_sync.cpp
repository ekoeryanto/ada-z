#include "time_sync.h"
#include "config.h"
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include <esp_sntp.h>
#include <cstring>
#include <ArduinoJson.h>
#include <vector>
#include "storage_helpers.h"
#include "sd_logger.h"

RTC_DS3231 rtc;
bool rtcFound = false;

static bool rtcEnabled = true;
static bool rtcLostPowerFlag = false;

static bool pendingRtcSync = false;
static bool awaitingSntp = false;

static unsigned long lastSyncRequestMillis = 0;
static unsigned long lastSyncSuccessMillis = 0;
static unsigned long lastSyncAttemptMillis = 0;
static unsigned long lastNtpAttempt = 0;
static unsigned long lastRtcAdjustMillis = 0;

static time_t cachedLastNtpEpoch = 0;
static String cachedLastNtpIso;
static bool cachedLastNtpIsoLoaded = false;

static const char* PREF_TIME_NS = "time";
static const char* PREF_LAST_NTP_EPOCH = "last_ntp";
static const char* PREF_LAST_NTP_ISO = "last_ntp_iso";
static const char* PREF_TIMEZONE = "tz";
static const char* PREF_NTP_SERVERS_JSON = "ntp_servers";
static const char* PREF_NTP_SYNC_MS = "ntp_sync_ms";
static const char* PREF_NTP_RETRY_MS = "ntp_retry_ms";

static const long MAX_RTC_DRIFT_SECONDS = 2;
static const unsigned long SYSTEM_TIME_TRUST_MS = 6UL * 3600UL * 1000UL; // 6 hours

static String currentTimezone = String(TIMEZONE);
static bool timezoneLoaded = false;
static std::vector<String> configuredNtpServers;
static bool ntpServersLoaded = false;
static unsigned long ntpSyncIntervalMs = NTP_SYNC_INTERVAL;
static unsigned long ntpRetryIntervalMs = NTP_RETRY_INTERVAL;
static bool intervalsLoaded = false;

static void applyTimezoneEnv(const String &tz) {
    if (tz.length() == 0) return;
    setenv("TZ", tz.c_str(), 1);
    tzset();
}

static void ensureTimezoneLoaded() {
    if (timezoneLoaded) return;
    String stored = loadStringFromNVSns(PREF_TIME_NS, PREF_TIMEZONE, String(""));
    if (stored.length() > 0) {
        currentTimezone = stored;
    } else {
        currentTimezone = String(TIMEZONE);
    }
    timezoneLoaded = true;
}

static void ensureIntervalsLoaded() {
    if (intervalsLoaded) return;
    unsigned long syncMs = loadULongFromNVSns(PREF_TIME_NS, PREF_NTP_SYNC_MS, (unsigned long)NTP_SYNC_INTERVAL);
    unsigned long retryMs = loadULongFromNVSns(PREF_TIME_NS, PREF_NTP_RETRY_MS, (unsigned long)NTP_RETRY_INTERVAL);
    if (syncMs < 60000UL) syncMs = NTP_SYNC_INTERVAL;
    if (retryMs < 1000UL) retryMs = NTP_RETRY_INTERVAL;
    ntpSyncIntervalMs = syncMs;
    ntpRetryIntervalMs = retryMs;
    intervalsLoaded = true;
}

static void ensureNtpServersLoaded() {
    if (ntpServersLoaded) return;
    configuredNtpServers.clear();
    String raw = loadStringFromNVSns(PREF_TIME_NS, PREF_NTP_SERVERS_JSON, String(""));
    if (raw.length() > 0) {
        JsonDocument doc;
        if (deserializeJson(doc, raw) == DeserializationError::Ok && doc.is<JsonArray>()) {
            for (JsonVariant v : doc.as<JsonArray>()) {
                if (v.is<const char*>()) {
                    String entry = String(v.as<const char*>());
                    entry.trim();
                    if (entry.length() > 0) configuredNtpServers.push_back(entry);
                }
            }
        }
    }
    if (configuredNtpServers.empty()) {
        for (size_t i = 0; i < NTP_SERVER_COUNT; ++i) {
            configuredNtpServers.push_back(String(NTP_SERVERS[i]));
        }
    }
    ntpServersLoaded = true;
}

static void persistNtpServers() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const String &s : configuredNtpServers) {
        arr.add(s);
    }
    String json;
    serializeJson(doc, json);
    saveStringToNVSns(PREF_TIME_NS, PREF_NTP_SERVERS_JSON, json);
}

static bool epochPlausible(time_t epoch) {
    if (epoch <= 0) return false;
    struct tm tm_chk;
    if (!gmtime_r(&epoch, &tm_chk)) return false;
    int year = tm_chk.tm_year + 1900;
    return year >= 2020 && year <= 2035;
}

static bool systemTimeIsValid() {
    time_t now = time(nullptr);
    return epochPlausible(now);
}

static bool systemTimeRecentlySynced() {
    if (lastSyncSuccessMillis == 0) return false;
    unsigned long nowMs = millis();
    return (nowMs - lastSyncSuccessMillis) <= SYSTEM_TIME_TRUST_MS;
}

static void setSystemTimeFromEpoch(time_t epoch) {
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
}

static void alignSystemTimeWithRtc();
static void persistLastNtp(time_t epoch, const String &iso);

static void IRAM_ATTR onSntpSync(struct timeval *tv) {
    time_t epoch = tv ? tv->tv_sec : time(nullptr);
    if (!epochPlausible(epoch)) return;

    awaitingSntp = false;
    lastSyncSuccessMillis = millis();
    cachedLastNtpEpoch = epoch;

    struct tm tm_utc;
    gmtime_r(&epoch, &tm_utc);
    char isoBuf[32];
    strftime(isoBuf, sizeof(isoBuf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    cachedLastNtpIso = String(isoBuf);
    persistLastNtp(epoch, cachedLastNtpIso);

    setSystemTimeFromEpoch(epoch);

    if (rtcEnabled && rtcFound && pendingRtcSync) {
        rtc.adjust(DateTime((uint32_t)epoch));
        pendingRtcSync = false;
        logErrorToSd(String("RTC updated from NTP: ") + cachedLastNtpIso);
        lastRtcAdjustMillis = millis();
    }

    alignSystemTimeWithRtc();
}

static void configureSntp() {
    sntp_stop();
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    sntp_set_time_sync_notification_cb(onSntpSync);
    ensureNtpServersLoaded();
    size_t serverCount = configuredNtpServers.size();
    for (size_t i = 0; i < serverCount && i < SNTP_MAX_SERVERS; ++i) {
        sntp_setservername(i, const_cast<char*>(configuredNtpServers[i].c_str()));
    }
    sntp_init();
}

time_t getLastNtpSuccessEpoch() {
    if (cachedLastNtpEpoch == 0) {
        cachedLastNtpEpoch = (time_t)loadULongFromNVS(PREF_LAST_NTP_EPOCH);
    }
    return cachedLastNtpEpoch;
}

String getLastNtpSuccessIso() {
    // Load from NVS at most once to avoid repeated NOT_FOUND logs when key missing
    if (!cachedLastNtpIsoLoaded) {
        cachedLastNtpIso = loadStringFromNVS(PREF_LAST_NTP_ISO, String(""));
        cachedLastNtpIsoLoaded = true;
    }
    return cachedLastNtpIso;
}

static void persistLastNtp(time_t epoch, const String &iso) {
    saveULongToNVS(PREF_LAST_NTP_EPOCH, (unsigned long)epoch);
    saveStringToNVS(PREF_LAST_NTP_ISO, iso);
}

static void ensureRtcToSystemAlignmentFromBoot() {
    if (!rtcFound || !rtcEnabled) return;
    time_t rtcEpoch = getRtcEpoch();
    if (!epochPlausible(rtcEpoch)) return;
    setSystemTimeFromEpoch(rtcEpoch);
}

void syncNtp(bool updateRtcAfter) {
    if (WiFi.status() != WL_CONNECTED) return;
    pendingRtcSync = (updateRtcAfter && rtcEnabled);
    awaitingSntp = true;
    lastSyncRequestMillis = millis();
    lastSyncAttemptMillis = lastSyncRequestMillis;
    lastNtpAttempt = lastSyncRequestMillis;
    configureSntp();
}

String getIsoTimestamp() {
    time_t sys = time(nullptr);
    if (epochPlausible(sys)) {
        char isoBuf[32];
        struct tm tm_sys;
        gmtime_r(&sys, &tm_sys);
        strftime(isoBuf, sizeof(isoBuf), "%Y-%m-%dT%H:%M:%SZ", &tm_sys);
        return String(isoBuf);
    }

    if (rtcFound) {
        time_t rtcEpoch = getRtcEpoch();
        if (epochPlausible(rtcEpoch)) {
            DateTime now = rtc.now();
            char buf[32];
            sprintf(buf, "%04d-%02d-%02dT%02d:%02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
            return String(buf);
        }
    }

    String last = getLastNtpSuccessIso();
    if (last.length() > 0) return last;

    char epochBuf[32];
    sprintf(epochBuf, "%lu", (unsigned long)sys);
    return String(epochBuf);
}

bool isRtcPresent() {
    return rtcFound;
}

time_t getRtcEpoch() {
    if (!rtcFound) return 0;
    DateTime now = rtc.now();
    struct tm tm_rtc;
    memset(&tm_rtc, 0, sizeof(tm_rtc));
    tm_rtc.tm_year = now.year() - 1900;
    tm_rtc.tm_mon = now.month() - 1;
    tm_rtc.tm_mday = now.day();
    tm_rtc.tm_hour = now.hour();
    tm_rtc.tm_min = now.minute();
    tm_rtc.tm_sec = now.second();
    tm_rtc.tm_isdst = -1;
    return mktime(&tm_rtc);
}

bool isPendingRtcSync() {
    return pendingRtcSync;
}

void setRtcEnabled(bool enabled) {
    rtcEnabled = enabled;
    saveULongToNVSns(PREF_TIME_NS, PREF_RTC_ENABLED, enabled ? 1UL : 0UL);
}

bool getRtcEnabled() {
    rtcEnabled = loadBoolFromNVSns(PREF_TIME_NS, PREF_RTC_ENABLED, DEFAULT_RTC_ENABLED != 0);
    return rtcEnabled;
}

bool isRtcLostPower() {
    return rtcLostPowerFlag;
}

void printCurrentTime() {
    if (!rtcFound) return;
    DateTime now = rtc.now();
    char buf[] = "YYYY/MM/DD hh:mm:ss";
    now.toString(buf);
    #if ENABLE_VERBOSE_LOGS
    Serial.print("RTC Time: ");
    Serial.print(buf);
    Serial.print(" | Temp: ");
    Serial.print(rtc.getTemperature());
    Serial.println(" C");
    #endif
}

static void alignSystemTimeWithRtc() {
    if (!rtcFound || !rtcEnabled) return;
    time_t rtcEpoch = getRtcEpoch();
    if (!epochPlausible(rtcEpoch)) return;

    bool sysValid = systemTimeIsValid();
    time_t sysEpoch = time(nullptr);

    if (!sysValid) {
        setSystemTimeFromEpoch(rtcEpoch);
        #if ENABLE_VERBOSE_LOGS
        Serial.println("System time restored from RTC (system time invalid).");
        #endif
        return;
    }

    long diff = labs((long)(rtcEpoch - sysEpoch));
    if (diff <= MAX_RTC_DRIFT_SECONDS) {
        return;
    }

    if (systemTimeRecentlySynced()) {
        // Trust the freshly synced system time and push it to RTC
        rtc.adjust(DateTime((uint32_t)sysEpoch));
        lastRtcAdjustMillis = millis();
        #if ENABLE_VERBOSE_LOGS
        Serial.printf("RTC realigned to system time (drift %ld seconds)\n", diff);
        #endif
    } else {
        setSystemTimeFromEpoch(rtcEpoch);
        #if ENABLE_VERBOSE_LOGS
        Serial.printf("System time realigned to RTC (drift %ld seconds)\n", diff);
        #endif
    }
}

void setupTimeSync() {
    ensureTimezoneLoaded();
    applyTimezoneEnv(currentTimezone);
    ensureIntervalsLoaded();
    ensureNtpServersLoaded();

    cachedLastNtpEpoch = (time_t)loadULongFromNVS(PREF_LAST_NTP_EPOCH);
    cachedLastNtpIso = loadStringFromNVS(PREF_LAST_NTP_ISO, String(""));
    if (cachedLastNtpEpoch > 0) {
        lastSyncSuccessMillis = millis();
    }

    rtcEnabled = (loadIntFromNVSns(PREF_TIME_NS, PREF_RTC_ENABLED, DEFAULT_RTC_ENABLED) != 0);

    if (rtcEnabled && rtc.begin()) {
        rtcFound = true;
        if (rtc.lostPower()) {
            Serial.println("RTC lost power, will require NTP sync to set RTC.");
            rtcLostPowerFlag = true;
            pendingRtcSync = true;
        } else {
            ensureRtcToSystemAlignmentFromBoot();
        }
    } else if (rtcEnabled) {
        Serial.println("RTC not found!");
        rtcFound = false;
    } else {
        rtcFound = false;
    }

    alignSystemTimeWithRtc();

    if (WiFi.status() == WL_CONNECTED) {
        syncNtp(true);
    }
}

void loopTimeSync() {
    alignSystemTimeWithRtc();

    unsigned long nowMillis = millis();

    ensureIntervalsLoaded();

    if (awaitingSntp) {
        if (nowMillis - lastSyncRequestMillis > ntpRetryIntervalMs) {
            syncNtp(pendingRtcSync);
        }
        return;
    }

    bool needSync = false;
    if (!systemTimeIsValid()) {
        needSync = true;
    } else if (nowMillis - lastSyncSuccessMillis > ntpSyncIntervalMs) {
        needSync = true;
    }

    if (needSync && nowMillis - lastSyncAttemptMillis > ntpRetryIntervalMs) {
        syncNtp(true);
        return;
    }

    if (rtcFound && rtcEnabled && systemTimeRecentlySynced()) {
        if (nowMillis - lastRtcAdjustMillis > SYSTEM_TIME_TRUST_MS) {
            time_t sysEpoch = time(nullptr);
            if (epochPlausible(sysEpoch)) {
                rtc.adjust(DateTime((uint32_t)sysEpoch));
                lastRtcAdjustMillis = nowMillis;
                #if ENABLE_VERBOSE_LOGS
                Serial.println("Periodic RTC alignment to system time.");
                #endif
            }
        }
    }
}

String formatIsoWithTz(time_t epoch) {
    time_t target = epoch;
    if (!epochPlausible(target)) {
        target = time(nullptr);
    }
    struct tm tm_local;
    localtime_r(&target, &tm_local);
    char buf[40];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &tm_local);
    String out(buf);
    if (out.length() > 5) {
        int split = out.length() - 2;
        out = out.substring(0, split) + ":" + out.substring(split);
    }
    return out;
}

String getTimezone() {
    ensureTimezoneLoaded();
    return currentTimezone;
}

void setTimezone(const String &tz) {
    String trimmed = tz;
    trimmed.trim();
    if (trimmed.length() == 0) {
        trimmed = String(TIMEZONE);
    }
    ensureTimezoneLoaded();
    if (trimmed == currentTimezone) {
        applyTimezoneEnv(currentTimezone);
        return;
    }
    currentTimezone = trimmed;
    timezoneLoaded = true;
    saveStringToNVSns(PREF_TIME_NS, PREF_TIMEZONE, currentTimezone);
    applyTimezoneEnv(currentTimezone);
}

std::vector<String> getConfiguredNtpServers() {
    ensureNtpServersLoaded();
    return configuredNtpServers;
}

void setConfiguredNtpServers(const std::vector<String> &servers) {
    configuredNtpServers.clear();
    for (const String &s : servers) {
        String entry = s;
        entry.trim();
        if (entry.length() > 0) {
            configuredNtpServers.push_back(entry);
        }
    }
    if (configuredNtpServers.empty()) {
        for (size_t i = 0; i < NTP_SERVER_COUNT; ++i) {
            configuredNtpServers.push_back(String(NTP_SERVERS[i]));
        }
    }
    ntpServersLoaded = true;
    persistNtpServers();
    configureSntp();
}

unsigned long getNtpSyncInterval() {
    ensureIntervalsLoaded();
    return ntpSyncIntervalMs;
}

void setNtpSyncInterval(unsigned long ms) {
    if (ms < 60000UL) ms = 60000UL;
    ntpSyncIntervalMs = ms;
    intervalsLoaded = true;
    saveULongToNVSns(PREF_TIME_NS, PREF_NTP_SYNC_MS, ntpSyncIntervalMs);
}

unsigned long getNtpRetryInterval() {
    ensureIntervalsLoaded();
    return ntpRetryIntervalMs;
}

void setNtpRetryInterval(unsigned long ms) {
    if (ms < 1000UL) ms = 1000UL;
    ntpRetryIntervalMs = ms;
    intervalsLoaded = true;
    saveULongToNVSns(PREF_TIME_NS, PREF_NTP_RETRY_MS, ntpRetryIntervalMs);
}
