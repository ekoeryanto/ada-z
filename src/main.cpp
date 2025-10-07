#include <Arduino.h>
#include "pins_config.h"
#include "config.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <RTClib.h>
#include <time.h>
#include <SPI.h>
#include <SD.h>
#include <HTTPClient.h>
#include "http_notifier.h"
#include "time_sync.h"
#include "sd_logger.h"
#include "storage_helpers.h"
#include "voltage_pressure_sensor.h"
#include "sample_store.h"
#include "ota_updater.h"
#include "wifi_manager_module.h"
#include "storage_helpers.h"
#include "web_api.h"
#include "web_api_common.h"
#include "json_helper.h"
#include "i2c_helpers.h"
#include "sensors_config.h"
#include "current_pressure_sensor.h"
#include "device_id.h"
#include "modbus_manager.h"

#include "nvs_flash.h"
#include "nvs_defaults.h"

// Globals

// Automatic SSE push tuning
static float *lastSentValue = nullptr;             // last value we pushed for each sensor
static unsigned long *lastSentMillis = nullptr;    // cooldown timer per sensor
const float SSE_PUSH_DELTA = 0.02f; // push when value changes more than 0.02 (2% of full scale-ish)
const unsigned long SSE_PUSH_COOLDOWN_MS = 2000; // don't push more than once per sensor within 2s

// Timers
unsigned long previousSensorMillis = 0;
unsigned long previousTimePrintMillis = 0;
static unsigned long lastBatchNotificationMillis = 0;
// Per-sensor settings (allocated in setup)
static bool *sensorEnabled = nullptr;
static unsigned long *sensorNotificationInterval = nullptr;
static unsigned long *sensorLastNotificationMillis = nullptr;
static int configuredNumSensors = 0;


// Flags

// Sensor EMA filter

// --- Function Prototypes ---
float convert010V(int adc);








// --- Main Setup & Loop ---
void setup() {
    Serial.begin(115200);
    // Initialize I2C centrally
    initI2C();

    // Ensure NVS is initialized before any Preferences usage
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs erase
        nvs_flash_erase();
        nvs_err = nvs_flash_init();
    }
    if (nvs_err != ESP_OK) {
        Serial.printf("[WARN] NVS init failed: 0x%08x\n", nvs_err);
        // Log to SD if available
        String msg = String("NVS init failed: 0x") + String((int)nvs_err, HEX);
        logErrorToSd(msg);
    }

    // Ensure default NVS keys exist to avoid Preferences NOT_FOUND spam
    ensureNvsDefaults();

    // Initialize ADS1115 for water pressure current sensors on A0/A1 (raw readings, no smoothing)
    if (!setupCurrentPressureSensor(ADS1115_ADDR)) {
        Serial.println("Warning: ADS1115 not detected — A0/A1 current sensors will be unavailable");
    }
    

    setupSdLogger();

    // Configure ADC resolution and attenuation for 0-10V sensors (using voltage divider)
    analogReadResolution(12); // 12-bit ADC
    analogSetPinAttenuation(AI1_PIN, ADC_11db);
    analogSetPinAttenuation(AI2_PIN, ADC_11db);
    analogSetPinAttenuation(AI3_PIN, ADC_11db);

    // Initialize ADC calibration and voltage sensor after attenuation is set
    initAdcCalibration();
    setupVoltagePressureSensor(); // Initialize voltage pressure sensor (seed smoothed values after ADC configured)

    // Initialize sample store: store samplesPerSensor (smaller => less averaging)
    // Reduced from 12 to 6 and now to 4 for even more responsive averaging during calibration/debug
    int samplesPerSensor = loadIntFromNVSns("adc_cfg", "sps", 4);
    initSampleStore(getNumVoltageSensors(), samplesPerSensor);

    // Load per-sensor enable flags and notification intervals from Preferences
    int numSensors = getNumVoltageSensors();
    configuredNumSensors = numSensors;
    sensorEnabled = new bool[numSensors];
    sensorNotificationInterval = new unsigned long[numSensors];
    sensorLastNotificationMillis = new unsigned long[numSensors];

    for (int i = 0; i < numSensors; ++i) {
        String enKey = String(PREF_SENSOR_ENABLED_PREFIX) + String(i);
        String ivKey = String(PREF_SENSOR_INTERVAL_PREFIX) + String(i);
        // Preferences doesn't have getBool on some libs; use int fallback
        bool enabled = loadIntFromNVSns("sensors", enKey.c_str(), DEFAULT_SENSOR_ENABLED ? 1 : 0) != 0;
        unsigned long interval = loadULongFromNVSns("sensors", ivKey.c_str(), DEFAULT_SENSOR_NOTIFICATION_INTERVAL);
        sensorEnabled[i] = enabled;
        sensorNotificationInterval[i] = interval;
        sensorLastNotificationMillis[i] = 0;
        #if ENABLE_VERBOSE_LOGS
        Serial.printf("Sensor %d enabled=%d interval=%lu\n", i, enabled, interval);
        #endif
    }

    // Initialize sensor runtime settings module
    initSensorRuntimeSettings();

    // Allocate SSE push tracking arrays
    if (configuredNumSensors > 0) {
        lastSentValue = new float[configuredNumSensors];
        lastSentMillis = new unsigned long[configuredNumSensors];
        for (int i = 0; i < configuredNumSensors; ++i) {
            lastSentValue[i] = NAN; // not sent yet
            lastSentMillis[i] = 0;
        }
    }

    setupTimeSync();
    setupAndConnectWiFi(); // Setup and connect to WiFi

    

    String mdnsName = WiFi.getHostname();
    if (!MDNS.begin(mdnsName.c_str())) {
        Serial.println("Error setting up MDNS responder!");
    }
    Serial.print("mDNS responder started as: ");
    Serial.println(mdnsName);

    setupOtaUpdater(); // Initialize OTA updater

    

    Serial.println("Ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    // Start HTTP API server
    setupWebServer();

    setupModbus();
}

// --- Sensors runtime + persistence API (exposed via sensors_config.h) ---
void initSensorRuntimeSettings() {
    // Nothing extra needed here; arrays already initialized in setup
}

int getConfiguredNumSensors() {
    return configuredNumSensors;
}

bool getSensorEnabled(int index) {
    if (!sensorEnabled) return false;
    if (index < 0 || index >= configuredNumSensors) return false;
    return sensorEnabled[index];
}

void setSensorEnabled(int index, bool enabled) {
    if (!sensorEnabled) return;
    if (index < 0 || index >= configuredNumSensors) return;
    sensorEnabled[index] = enabled;
}

unsigned long getSensorNotificationInterval(int index) {
    if (!sensorNotificationInterval) return HTTP_NOTIFICATION_INTERVAL;
    if (index < 0 || index >= configuredNumSensors) return HTTP_NOTIFICATION_INTERVAL;
    return sensorNotificationInterval[index];
}

void setSensorNotificationInterval(int index, unsigned long interval) {
    if (!sensorNotificationInterval) return;
    if (index < 0 || index >= configuredNumSensors) return;
    sensorNotificationInterval[index] = interval;
}

void persistSensorSettings() {
    for (int i = 0; i < configuredNumSensors; ++i) {
        String enKey = String(PREF_SENSOR_ENABLED_PREFIX) + String(i);
        String ivKey = String(PREF_SENSOR_INTERVAL_PREFIX) + String(i);
        saveIntToNVSns("sensors", enKey.c_str(), sensorEnabled[i] ? 1 : 0);
        saveULongToNVSns("sensors", ivKey.c_str(), sensorNotificationInterval[i]);
    }
}

void loop() {
    

    loopTimeSync();
    loopModbus();

    // Non-blocking sensor reading and logging
    unsigned long currentMillis = millis();
    static unsigned long lastPendingFlushMillis = 0;

    if (currentMillis - previousSensorMillis >= SENSOR_READ_INTERVAL) {
        previousSensorMillis = currentMillis;
        // Update all configured voltage sensors and collect logging values
        for (int i = 0; i < getNumVoltageSensors(); ++i) {
            updateVoltagePressureSensor(i);
        }
        // Build CSV: timestamp, then for each sensor: raw, smoothed, voltage
        String dataString = "";
        if (rtcFound) {
            DateTime now = rtc.now();
            char timestamp[25];
            sprintf(timestamp, "%04d-%02d-%02dT%02d:%02d:%02d",
                    now.year(), now.month(), now.day(),
                    now.hour(), now.minute(), now.second());
            dataString += timestamp;
        }

        // Prepare arrays for batch notification only for enabled & due sensors
        int totalSensors = getNumVoltageSensors();
        int *rawVals = new int[totalSensors];
        float *smoothedVals = new float[totalSensors];
        float *voltVals = new float[totalSensors];
        std::vector<int> dueSensorIndices;
        int batchCount = 0;

        unsigned long now = millis();
        for (int i = 0; i < totalSensors; ++i) {
            int raw = analogRead(getVoltageSensorPin(i));
            float smoothed = getSmoothedADC(i);
            float volt = getSmoothedVoltagePressure(i);

            // Persist the sample into in-memory store for later averaging
            addSample(i, raw, smoothed, volt);

            int mv_raw = adcRawToMv(raw);
            int mv_sm = adcRawToMv(static_cast<int>(round(smoothed)));
            dataString += "," + String(raw) + "," + String(smoothed) + "," + String(volt) + "," + String(mv_raw) + "," + String(mv_sm);
            #if ENABLE_VERBOSE_LOGS
            Serial.printf("AI%d Pin %d (raw): %d | (smoothed): %.2f | Voltage: %.3f V | mV_raw: %d mV | mV_smoothed: %d mV\n", i+1, getVoltageSensorPin(i), raw, smoothed, volt, mv_raw, mv_sm);
            #endif

            if (sensorEnabled && sensorEnabled[i]) {
                unsigned long interval = sensorNotificationInterval ? sensorNotificationInterval[i] : HTTP_NOTIFICATION_INTERVAL;
                if (now - sensorLastNotificationMillis[i] >= interval) {
                    dueSensorIndices.push_back(i);
                    sensorLastNotificationMillis[i] = now; // Update timestamp immediately

                    // Use averages from sample store for cleaner notification values if available
                    float avgRaw, avgSmoothed, avgVolt;
                    if (getAverages(i, avgRaw, avgSmoothed, avgVolt)) {
                        rawVals[i] = static_cast<int>(round(avgRaw));
                        smoothedVals[i] = avgSmoothed;
                    } else {
                        rawVals[i] = raw;
                        smoothedVals[i] = smoothed;
                    }
                }
            }

            // Automatic SSE push on significant change (independent of HTTP notification)
            if (lastSentValue && lastSentMillis) {
                bool send = false;
                float prev = lastSentValue[i];
                unsigned long lm = lastSentMillis[i];
                if (isnan(prev)) {
                    // never sent before, send initial
                    send = true;
                } else {
                    float diff = fabs(volt - prev);
                    if (diff >= SSE_PUSH_DELTA) send = true;
                }
                if (send && (millis() - lm >= SSE_PUSH_COOLDOWN_MS)) {
                    // Build simple JSON payload and push
                    JsonDocument p;
                    p["pin_index"] = i;
                    p["tag"] = String("AI") + String(i + 1);
                    p["value"] = roundToDecimals(volt, 3);
                    p["smoothed"] = roundToDecimals(smoothed, 3);
                    p["raw"] = raw;
                    String out; serializeJson(p, out);
                    pushSseDebugMessage("sensor_debug", out);
                    lastSentValue[i] = volt;
                    lastSentMillis[i] = millis();
                }
            }
        }

        if (!dueSensorIndices.empty()) {
        // Build minimal batch JSON payload for pending notification storage
        JsonDocument doc;
        doc["timestamp"] = getIsoTimestamp();
            doc["rtu"] = String(getChipId());
            JsonArray tags = doc["tags"].to<JsonArray>();
            for (int k = 0; k < (int)dueSensorIndices.size(); ++k) {
                int si = dueSensorIndices[k];
                JsonObject obj = tags.add<JsonObject>();
                obj["id"] = String("AI") + String(si + 1);
                obj["index"] = si;
                obj["raw"] = rawVals[si];
                obj["filtered"] = smoothedVals[si];
                obj["value"] = getSmoothedVoltagePressure(si);
            }
            String payload;
            serializeJson(doc, payload);

            // Append pending notification to SD (backup)
            appendPendingNotification(payload);

            // Attempt immediate send as well
            // sendHttpNotificationBatch(dueSensorIndices.size(), dueSensorIndices.data(), rawVals, smoothedVals);
        }

        delete[] rawVals;
        delete[] smoothedVals;
        delete[] voltVals;

        flagSensorsSnapshotUpdate();

        // Append ADS1115 A0/A1 readings (raw, mV, mA) to CSV and serial output
        for (int ch = 0; ch <= 1; ++ch) {
            int16_t rawAds = readAdsRaw(ch);
            float mv = adsRawToMv(rawAds);
            float shunt = getAdsShuntOhm(ch);
            float ampGain = getAdsAmpGain(ch);
            float ma = readAdsMa(ch, shunt, ampGain);
            float depth = computeDepthMm(ma, DEFAULT_CURRENT_INIT_MA, DEFAULT_RANGE_MM, DEFAULT_DENSITY_WATER);
            dataString += "," + String(rawAds) + "," + String(mv) + "," + String(ma) + "," + String(depth);
            #if ENABLE_VERBOSE_LOGS
            Serial.printf("ADS A%d raw: %d | mv: %.2f mV | ma: %.3f mA | depth: %.1f mm\n", ch, rawAds, mv, ma, depth);
            #endif
        }

        // Always log the CSV data to SD
        logSensorDataToSd(dataString);
    }

    // Periodic batch notification independent of sensor read loop
    if (currentMillis - lastBatchNotificationMillis >= HTTP_NOTIFICATION_INTERVAL) {
        lastBatchNotificationMillis = currentMillis;

        int totalSensors = getNumVoltageSensors();
        std::vector<int> allSensorIndices;
        int *rawVals = new int[totalSensors];
        float *smoothedVals = new float[totalSensors];

        for (int i = 0; i < totalSensors; ++i) {
            if (getSensorEnabled(i)) {
                allSensorIndices.push_back(i);
                float avgRaw, avgSmoothed, avgVolt;
                if (getAverages(i, avgRaw, avgSmoothed, avgVolt)) {
                    rawVals[i] = static_cast<int>(round(avgRaw));
                    smoothedVals[i] = avgSmoothed;
                } else {
                    rawVals[i] = analogRead(getVoltageSensorPin(i));
                    smoothedVals[i] = getSmoothedADC(i);
                }
            }
        }

        if (!allSensorIndices.empty()) {
            sendHttpNotificationBatch(allSensorIndices.size(), allSensorIndices.data(), rawVals, smoothedVals);
        }

        delete[] rawVals;
        delete[] smoothedVals;
    }

    // Periodic flush: every 5 minutes attempt to flush pending notifications from SD
    if (currentMillis - lastPendingFlushMillis >= 5UL * 60UL * 1000UL) {
        lastPendingFlushMillis = currentMillis;
        if (getSdEnabled() && sdCardFound) {
            bool ok = flushPendingNotifications();
            if (!ok) Serial.println("Pending notifications flush failed (will retry later)");
            else Serial.println("Pending notifications flushed successfully");
        }
    }

    // Non-blocking time printing
    if (currentMillis - previousTimePrintMillis >= PRINT_TIME_INTERVAL) {
        previousTimePrintMillis = currentMillis;
        printCurrentTime();
    }

    // Service web server
    // Service OTA and web server
    serviceWifiManager();
    handleOtaUpdate(); // This handles ArduinoOTA, which is separate
    serviceSensorsSnapshotUpdates();
    // handleWebServerClients() is no longer needed with ESPAsyncWebServer
}
