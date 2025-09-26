#include "sd_logger.h"
#include <SPI.h> // Required for SD library
#include "pins_config.h" // For SD_CS pin

// Global variable defined here
#include "sd_logger.h"
#include <SPI.h> // Required for SD library
#include "pins_config.h" // For SD_CS pin
#include <Preferences.h>
#include "config.h"

// Global variable defined here
bool sdCardFound = false;
static bool sdEnabled = true;
static Preferences sdPreferences;

void setupSdLogger() {
    // Load persisted SD enabled flag
    sdPreferences.begin("sd", false);
    sdEnabled = sdPreferences.getInt(PREF_SD_ENABLED, DEFAULT_SD_ENABLED) != 0;
    sdPreferences.end();

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
    sdPreferences.begin("sd", false);
    sdPreferences.putInt(PREF_SD_ENABLED, enabled ? 1 : 0);
    sdPreferences.end();
    sdEnabled = enabled;
    if (!sdEnabled) sdCardFound = false;
}

bool getSdEnabled() {
    sdPreferences.begin("sd", false);
    int v = sdPreferences.getInt(PREF_SD_ENABLED, DEFAULT_SD_ENABLED);
    sdPreferences.end();
    sdEnabled = (v != 0);
    return sdEnabled;
}

