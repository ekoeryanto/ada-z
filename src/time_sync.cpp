#include "time_sync.h"
#include "config.h"
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include <esp_sntp.h>
#include <cstring>
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

static time_t cachedLastNtpEpoch = 0;
static String cachedLastNtpIso;

static const char* PREF_TIME_NS = "time";
static const char* PREF_LAST_NTP_EPOCH = "last_ntp";
static const char* PREF_LAST_NTP_ISO = "last_ntp_iso";

static const long MAX_RTC_DRIFT_SECONDS = 2;

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
    }

    alignSystemTimeWithRtc();
}

static void configureSntp() {
    sntp_stop();
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    sntp_set_time_sync_notification_cb(onSntpSync);
    for (size_t i = 0; i < NTP_SERVER_COUNT && i < SNTP_MAX_SERVERS; ++i) {
        sntp_setservername(i, (char*)NTP_SERVERS[i]);
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
    if (cachedLastNtpIso.length() == 0) {
        cachedLastNtpIso = loadStringFromNVS(PREF_LAST_NTP_ISO, String(""));
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

    if (!systemTimeIsValid()) {
        setSystemTimeFromEpoch(rtcEpoch);
        Serial.println("System time restored from RTC (system time invalid).");
        return;
    }

    time_t sysEpoch = time(nullptr);
    long diff = labs((long)(rtcEpoch - sysEpoch));
    if (diff > MAX_RTC_DRIFT_SECONDS) {
        setSystemTimeFromEpoch(rtcEpoch);
        #if ENABLE_VERBOSE_LOGS
        Serial.printf("System time realigned to RTC (drift %ld seconds)\n", diff);
        #endif
    }
}

void setupTimeSync() {
    setenv("TZ", TIMEZONE, 1);
    tzset();

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
}

void loopTimeSync() {
    alignSystemTimeWithRtc();

    unsigned long nowMillis = millis();

    if (awaitingSntp) {
        if (nowMillis - lastSyncRequestMillis > NTP_RETRY_INTERVAL) {
            syncNtp(pendingRtcSync);
        }
        return;
    }

    bool needSync = false;
    if (!systemTimeIsValid()) {
        needSync = true;
    } else if (nowMillis - lastSyncSuccessMillis > NTP_SYNC_INTERVAL) {
        needSync = true;
    }

    if (needSync && nowMillis - lastSyncAttemptMillis > NTP_RETRY_INTERVAL) {
        syncNtp(true);
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
