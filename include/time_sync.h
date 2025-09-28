#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <Arduino.h>
#include <RTClib.h>
#include <vector>

// Function prototypes for time synchronization
void setupTimeSync();
void loopTimeSync();
void printCurrentTime();
void syncNtp(bool updateRtcAfter = true); // Expose syncNtp for initial call after WiFi connect

// Accessors for status
time_t getLastNtpSuccessEpoch();
bool isRtcPresent();
time_t getRtcEpoch();
bool isPendingRtcSync();
String getLastNtpSuccessIso();
String getIsoTimestamp();
// Format a specific epoch into ISO8601 with timezone offset (local) like 2025-09-28T14:13:28+07:00
String formatIsoWithTz(time_t epoch);

// Runtime-configurable time settings
String getTimezone();
void setTimezone(const String &tz);
std::vector<String> getConfiguredNtpServers();
void setConfiguredNtpServers(const std::vector<String> &servers);
unsigned long getNtpSyncInterval();
void setNtpSyncInterval(unsigned long ms);
unsigned long getNtpRetryInterval();
void setNtpRetryInterval(unsigned long ms);

// RTC enable/disable control
void setRtcEnabled(bool enabled);
bool getRtcEnabled();

// Extern declarations for global variables used by time sync
extern RTC_DS3231 rtc;
extern bool rtcFound;

// Expose whether the RTC has lost power since last battery backup
bool isRtcLostPower();

#endif // TIME_SYNC_H
