#include "sd_manager.h"
#include "storage_helpers.h"
#include <WiFi.h>
#include <HTTPClient.h>
// time helpers from project (isRtcPresent, getRtcEpoch, getIsoTimestamp)
#include "time_sync.h"
#include <vector>

// Config
static uint8_t csPinGlobal = 5;
static const char* LOG_PATH = "/sensor_log.csv"; // CSV: epoch,value\n
static const char* PREF_LAST_UPLOADED = "last_uploaded_epoch"; // key in NVS
static const char* PREF_NAMESPACE_LOCAL = "sd_mgr"; // separate namespace for sd manager prefs

static String uploadUrl = "";
static String deviceIdGlobal = "";
static String apiTokenGlobal = "";

// Timers
static unsigned long lastLogMs = 0;
static unsigned long lastUploadMs = 0;

// Flags
static bool sdReady = false;

bool sdManagerBegin(uint8_t csPin, uint32_t spiFreq) {
    csPinGlobal = csPin;
    if (!SD.begin(csPinGlobal)) {
        Serial.println("SD.begin failed");
        sdReady = false;
        return false;
    }
    sdReady = true;
    Serial.println("SD initialized");
    // Ensure log file exists
    if (!SD.exists(LOG_PATH)) {
        File f = SD.open(LOG_PATH, FILE_WRITE);
        if (f) f.close();
    }
    lastLogMs = millis();
    lastUploadMs = millis();
    return true;
}

const char* sdLogPath() { return LOG_PATH; }

void sdManagerSetUploadUrl(const char* url) { uploadUrl = String(url); }
void sdManagerSetDeviceInfo(const String &deviceId, const String &apiToken) { deviceIdGlobal = deviceId; apiTokenGlobal = apiToken; }

// Read sensor (user asked to provide). We'll implement analogRead here for real sensors.
float readSensor(int sensorPin) {
    int raw = analogRead(sensorPin);
    // convert to voltage/pressure if needed elsewhere; return raw as float here
    return (float)raw;
}

bool logToSD(const String &csvLine) {
    if (!sdReady) return false;
    File f = SD.open(LOG_PATH, FILE_APPEND);
    if (!f) return false;
    f.println(csvLine);
    f.close();
    return true;
}

// Internal: get last_uploaded_epoch from Preferences
static unsigned long getLastUploadedEpoch() {
    return loadULongFromNVSns(PREF_NAMESPACE_LOCAL, PREF_LAST_UPLOADED, 0UL);
}
static void setLastUploadedEpoch(unsigned long epoch) {
    saveULongToNVSns(PREF_NAMESPACE_LOCAL, PREF_LAST_UPLOADED, epoch);
}

// Read CSV and collect rows newer than (now - minutes)
static std::vector<String> collectRecentRows(int minutes) {
    std::vector<String> rows;
    if (!sdReady) return rows;
    File f = SD.open(LOG_PATH, FILE_READ);
    if (!f) return rows;
    unsigned long nowEpoch = (unsigned long)(isRtcPresent() ? getRtcEpoch() : time(nullptr));
    unsigned long threshold = (nowEpoch > (unsigned long)minutes * 60) ? (nowEpoch - (unsigned long)minutes * 60) : 0;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        // parse epoch as first csv column
        int comma = line.indexOf(',');
        if (comma <= 0) continue;
        String sEpoch = line.substring(0, comma);
        unsigned long ep = sEpoch.toInt();
        if (ep >= threshold) rows.push_back(line);
    }
    f.close();
    return rows;
}

// upload rows via HTTP POST application/csv or JSON array
bool uploadBatchToCloud() {
    if (!sdReady) return false;
    if (uploadUrl.length() == 0) {
        Serial.println("Upload URL not configured");
        return false;
    }
    std::vector<String> rows = collectRecentRows(5); // last 5 minutes
    if (rows.size() == 0) {
        Serial.println("No recent rows to upload");
        return true; // nothing to do
    }

    // Build CSV body
    String body;
    for (auto &r : rows) {
        body += r;
        body += "\n";
    }

    HTTPClient http;
    http.begin(uploadUrl);
    http.addHeader("Content-Type", "text/csv");
    if (deviceIdGlobal.length()) http.addHeader("X-Device-Id", deviceIdGlobal);
    if (apiTokenGlobal.length()) http.addHeader("Authorization", String("Bearer ") + apiTokenGlobal);

    int code = http.POST((uint8_t*)body.c_str(), body.length());
    bool ok = false;
    if (code > 0) {
        Serial.printf("Upload HTTP code: %d\n", code);
        if (code >= 200 && code < 300) ok = true;
    } else {
        Serial.printf("HTTP POST failed: %s\n", http.errorToString(code).c_str());
    }
    http.end();

    if (ok) {
        // Mark uploaded rows by rotating file: create temp file with rows older than earliest uploaded epoch
        // Find earliest epoch uploaded
        unsigned long earliest = 0;
        for (auto &r : rows) {
            int comma = r.indexOf(',');
            if (comma <= 0) continue;
            unsigned long ep = r.substring(0, comma).toInt();
            if (earliest == 0 || ep < earliest) earliest = ep;
        }
        if (earliest == 0) return true;

        // Rewrite log with rows that have epoch > earliest (i.e., newer than uploaded) or < earliest (older) ???
        // We'll preserve rows with epoch > earliest (newer) and rows < earliest (older but not uploaded) — effectively remove uploaded rows with epoch >= earliest and <= latest_uploaded
        unsigned long latest = 0;
        for (auto &r : rows) {
            int comma = r.indexOf(',');
            if (comma <= 0) continue;
            unsigned long ep = r.substring(0, comma).toInt();
            if (ep > latest) latest = ep;
        }

        // Read original file and write to temp excluding uploaded range [earliest..latest]
        File fin = SD.open(LOG_PATH, FILE_READ);
        String tmpPath = String(LOG_PATH) + ".tmp";
        File fout = SD.open(tmpPath.c_str(), FILE_WRITE);
        if (!fin || !fout) {
            if (fin) fin.close();
            if (fout) fout.close();
            Serial.println("Failed to open files for rotation after upload");
            return true; // uploaded ok, but cannot rotate; keep data as-is
        }
        while (fin.available()) {
            String line = fin.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;
            int comma = line.indexOf(',');
            if (comma <= 0) continue;
            unsigned long ep = line.substring(0, comma).toInt();
            // keep line if it's outside uploaded interval
            if (ep < earliest || ep > latest) {
                fout.println(line);
            }
        }
        fin.close(); fout.close();
        // replace original
        SD.remove(LOG_PATH);
        SD.rename(tmpPath.c_str(), LOG_PATH);

        // persist last_uploaded_epoch
        setLastUploadedEpoch(latest);
        Serial.printf("Upload succeeded, removed rows between %lu and %lu\n", earliest, latest);
    } else {
        Serial.println("Upload failed, keeping data on SD");
    }
    return ok;
}

void sdManagerForceUploadNow() {
    lastUploadMs = 0; // force immediate upload on next loop
}

void sdManagerLoop() {
    // 1-second logging
    if (millis() - lastLogMs >= 1000) {
        lastLogMs += 1000;
        // read sensor(s) and append
        // Example: read sensor on pin 33 — adapt as needed in your project
        float val = readSensor(33);
        unsigned long epoch = (unsigned long)(isRtcPresent() ? getRtcEpoch() : time(nullptr));
        String line = String(epoch) + "," + String(val, 2);
        bool ok = logToSD(line);
        if (!ok) Serial.println("Failed to log to SD");
    }

    // 5-minute upload
    if (millis() - lastUploadMs >= 5 * 60 * 1000) {
        lastUploadMs = millis();
        if (WiFi.status() == WL_CONNECTED) {
            bool ok = uploadBatchToCloud();
            (void)ok;
        } else {
            Serial.println("WiFi not connected, skipping upload");
        }
    }
}

