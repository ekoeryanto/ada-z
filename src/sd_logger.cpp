#include "sd_logger.h"
#include <SPI.h> // Required for SD library
#include "pins_config.h" // For SD_CS pin
#include "config.h"
#include "storage_helpers.h"
#include <WiFi.h>
#include <HTTPClient.h>

// Global variable defined here
bool sdCardFound = false;
static bool sdEnabled = true;

void setupSdLogger() {
    // Load persisted SD enabled flag
    sdEnabled = loadBoolFromNVSns("sd", PREF_SD_ENABLED, DEFAULT_SD_ENABLED != 0);

    if (!sdEnabled) {
        Serial.println("SD logging disabled by configuration.");
        sdCardFound = false;
        return;
    }

    if (SD.begin(SD_CS)) {
        sdCardFound = true;
        Serial.println("SD card initialized.");
        uint64_t cardSize = SD.cardSize() / (1024 * 1024);
        Serial.printf("SD Card Size: %lluMB\n", cardSize);
        // Create header for log file if it doesn't exist
        File dataFile = SD.open("/datalog.csv");
        if (!dataFile) {
            Serial.println("Creating datalog.csv with header...");
            dataFile = SD.open("/datalog.csv", FILE_WRITE);
            if (dataFile) {
                dataFile.println("Timestamp,RawADC,SmoothedADC,Voltage");
                dataFile.close();
            }
        }
        dataFile.close();
    } else {
        Serial.println("SD card initialization failed!");
    }
}

void logSensorDataToSd(String data) {
    if (!sdCardFound) return;

    File dataFile = SD.open("/datalog.csv", FILE_APPEND);
    if (dataFile) {
        dataFile.println(data);
        dataFile.close();
        Serial.println("Data logged to SD card.");
    } else {
        Serial.println("Error opening datalog.csv");
    }
}

// Append a JSON line to pending notification file. Returns true on success.
bool appendPendingNotification(const String &jsonLine) {
    if (!sdCardFound) return false;
    File f = SD.open("/pending_notifications.jsonl", FILE_APPEND);
    if (!f) return false;
    f.println(jsonLine);
    f.close();
    return true;
}

// Flush pending notifications: read the file and attempt to send via HTTP using existing upload logic
bool flushPendingNotifications() {
    if (!sdCardFound) return false;
    if (WiFi.status() != WL_CONNECTED) return false;
    // Read all pending lines
    File f = SD.open("/pending_notifications.jsonl", FILE_READ);
    if (!f) return false;
    String body;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        body += line;
        body += "\n";
    }
    f.close();

    if (body.length() == 0) return true; // nothing to do

    // Send as text/plain (line-delimited JSON) to configured HTTP_NOTIFICATION_URL
    HTTPClient http;
    http.begin(HTTP_NOTIFICATION_URL);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST((uint8_t*)body.c_str(), body.length());
    bool ok = false;
    if (code > 0 && code >= 200 && code < 300) ok = true;
    http.end();

    if (ok) {
        // remove pending file
        SD.remove("/pending_notifications.jsonl");
    }
    return ok;
}

String readPendingNotifications(int maxLines) {
    if (!sdCardFound) return String();
    File f = SD.open("/pending_notifications.jsonl", FILE_READ);
    if (!f) return String();

    String out;
    int lines = 0;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        if (line.length() == 0) continue;
        out += line + "\n";
        lines++;
        if (maxLines >= 0 && lines >= maxLines) break;
    }
    f.close();
    return out;
}

bool clearPendingNotifications() {
    if (!sdCardFound) return false;
    if (!SD.exists("/pending_notifications.jsonl")) return true;
    return SD.remove("/pending_notifications.jsonl");
}

size_t countPendingNotifications() {
    if (!sdCardFound) return 0;
    File f = SD.open("/pending_notifications.jsonl", FILE_READ);
    if (!f) return 0;
    size_t count = 0;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        if (line.length() == 0) continue;
        count++;
    }
    f.close();
    return count;
}

size_t pendingNotificationsFileSize() {
    if (!sdCardFound) return 0;
    File f = SD.open("/pending_notifications.jsonl", FILE_READ);
    if (!f) return 0;
    size_t sz = f.size();
    f.close();
    return sz;
}

// Append an error message with timestamp to /error.log
#include "time_sync.h"
void logErrorToSd(const String &msg) {
    if (!sdCardFound) return;
    File f = SD.open("/error.log", FILE_APPEND);
    if (!f) {
        Serial.println("Error opening error.log for append");
        return;
    }
    // Include ISO timestamp if time available
    extern String getIsoTimestamp();
    String ts = getIsoTimestamp();
    f.print(ts);
    f.print(" ");
    f.println(msg);
    f.close();
}

// Read error log; return up to maxLines (if maxLines < 0, return whole file)
String readErrorLog(int maxLines) {
    if (!sdCardFound) return String();
    File f = SD.open("/error.log", FILE_READ);
    if (!f) return String();

    String out;
    int lines = 0;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        out += line + "\n";
        lines++;
        if (maxLines >= 0 && lines >= maxLines) break;
    }
    f.close();
    return out;
}

// Clear the error log
void clearErrorLog() {
    if (!sdCardFound) return;
    // Overwrite file by opening in write mode
    File f = SD.open("/error.log", FILE_WRITE);
    if (!f) return;
    f.close();
}

void setSdEnabled(bool enabled) {
    saveULongToNVSns("sd", PREF_SD_ENABLED, enabled ? 1UL : 0UL);
    sdEnabled = enabled;
    if (!sdEnabled) sdCardFound = false;
}

bool getSdEnabled() {
    sdEnabled = loadBoolFromNVSns("sd", PREF_SD_ENABLED, DEFAULT_SD_ENABLED != 0);
    return sdEnabled;
}
