#include <Arduino.h>
#include "pins_config.h"
#include "config.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
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
#include "voltage_pressure_sensor.h"
#include "sample_store.h"
#include "ota_updater.h"
#include "wifi_manager_module.h"
#include <Preferences.h>
#include "web_api.h"
#include "sensors_config.h"
#include "current_pressure_sensor.h"
#include "preferences_helper.h"

#include "nvs_flash.h"

// Globals

// Timers
unsigned long previousSensorMillis = 0;
unsigned long previousTimePrintMillis = 0;
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
    Wire.begin(I2C_SDA, I2C_SCL);

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
    Preferences p_adc;
    p_adc.begin("adc_cfg", true);
    int samplesPerSensor = p_adc.getInt("samples_per_sensor", 4);
    p_adc.end();
    initSampleStore(getNumVoltageSensors(), samplesPerSensor);

    // Load per-sensor enable flags and notification intervals from Preferences
    int numSensors = getNumVoltageSensors();
    configuredNumSensors = numSensors;
    sensorEnabled = new bool[numSensors];
    sensorNotificationInterval = new unsigned long[numSensors];
    sensorLastNotificationMillis = new unsigned long[numSensors];

    Preferences p;
    safePreferencesBegin(p, "sensors");
    for (int i = 0; i < numSensors; ++i) {
        String enKey = String(PREF_SENSOR_ENABLED_PREFIX) + String(i);
        String ivKey = String(PREF_SENSOR_INTERVAL_PREFIX) + String(i);
        // Preferences doesn't have getBool on some libs; use getInt fallback
        bool enabled = p.getInt(enKey.c_str(), DEFAULT_SENSOR_ENABLED ? 1 : 0) != 0;
        unsigned long interval = p.getULong(ivKey.c_str(), DEFAULT_SENSOR_NOTIFICATION_INTERVAL);
        sensorEnabled[i] = enabled;
        sensorNotificationInterval[i] = interval;
        sensorLastNotificationMillis[i] = 0;
        Serial.printf("Sensor %d enabled=%d interval=%lu\n", i, enabled, interval);
    }
    p.end();

    // Initialize sensor runtime settings module
    initSensorRuntimeSettings();

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
    Preferences p;
    p.begin("sensors", false);
    for (int i = 0; i < configuredNumSensors; ++i) {
        String enKey = String(PREF_SENSOR_ENABLED_PREFIX) + String(i);
        String ivKey = String(PREF_SENSOR_INTERVAL_PREFIX) + String(i);
        p.putInt(enKey.c_str(), sensorEnabled[i] ? 1 : 0);
        p.putULong(ivKey.c_str(), sensorNotificationInterval[i]);
    }
    p.end();
}

void loop() {
    

    loopTimeSync();

    // Non-blocking sensor reading and logging
    unsigned long currentMillis = millis();
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
        int batchCount = 0;

        unsigned long now = millis();
        for (int i = 0; i < totalSensors; ++i) {
            int raw = analogRead(getVoltageSensorPin(i));
            float smoothed = getSmoothedADC(i);
            float volt = getSmoothedVoltagePressure(i);

            // Persist the sample into in-memory store for later averaging
            addSample(i, raw, smoothed, volt);

            int mv_raw = adcRawToMv(raw);
            int mv_sm = adcRawToMv((int)round(smoothed));
            dataString += "," + String(raw) + "," + String(smoothed) + "," + String(volt) + "," + String(mv_raw) + "," + String(mv_sm);
            Serial.printf("AI%d Pin %d (raw): %d | (smoothed): %.2f | Voltage: %.3f V | mV_raw: %d mV | mV_smoothed: %d mV\n", i+1, getVoltageSensorPin(i), raw, smoothed, volt, mv_raw, mv_sm);

            if (sensorEnabled && sensorEnabled[i]) {
                unsigned long interval = sensorNotificationInterval ? sensorNotificationInterval[i] : HTTP_NOTIFICATION_INTERVAL;
                if (now - sensorLastNotificationMillis[i] >= interval) {
                    // Use averages from sample store for cleaner notification values if available
                    float avgRaw, avgSmoothed, avgVolt;
                    if (getAverages(i, avgRaw, avgSmoothed, avgVolt)) {
                        rawVals[batchCount] = (int)round(avgRaw);
                        smoothedVals[batchCount] = avgSmoothed;
                        voltVals[batchCount] = avgVolt;
                    } else {
                        rawVals[batchCount] = raw;
                        smoothedVals[batchCount] = smoothed;
                        voltVals[batchCount] = volt;
                    }
                    // Keep mapping for updating lastNotification: store sensor index in smoothedVals temporarily? No.
                    // We'll update lastNotificationMillis for included sensors by scanning again after send.
                    batchCount++;
                }
            }
        }

        if (batchCount > 0) {
            // Send batched notification for due sensors
            int *indices = new int[batchCount];
            // Recompute which sensors are included and fill indices in same order
            int idx = 0;
            for (int i = 0; i < totalSensors && idx < batchCount; ++i) {
                if (sensorEnabled && sensorEnabled[i]) {
                    unsigned long interval = sensorNotificationInterval ? sensorNotificationInterval[i] : HTTP_NOTIFICATION_INTERVAL;
                    if (now - sensorLastNotificationMillis[i] >= interval) {
                        indices[idx++] = i;
                    }
                }
            }
            sendHttpNotificationBatch(batchCount, indices, rawVals, smoothedVals);
            delete[] indices;
            // Update last notification time for sensors we sent
            // (Assume sensors are first batchCount sensors in order — map back by re-evaluating due condition)
            int idx2 = 0;
            for (int i = 0; i < totalSensors && idx2 < batchCount; ++i) {
                if (sensorEnabled && sensorEnabled[i]) {
                    unsigned long interval = sensorNotificationInterval ? sensorNotificationInterval[i] : HTTP_NOTIFICATION_INTERVAL;
                    if (now - sensorLastNotificationMillis[i] >= interval) {
                        sensorLastNotificationMillis[i] = now;
                        idx2++;
                    }
                }
            }
        }

        delete[] rawVals;
        delete[] smoothedVals;
        delete[] voltVals;

        // Append ADS1115 A0/A1 readings (raw, mV, mA) to CSV and serial output
        for (int ch = 0; ch <= 1; ++ch) {
            int16_t rawAds = readAdsRaw(ch);
            float mv = adsRawToMv(rawAds);
            float shunt = getAdsShuntOhm(ch);
            float ampGain = getAdsAmpGain(ch);
            float ma = readAdsMa(ch, shunt, ampGain);
            float depth = computeDepthMm(ma, DEFAULT_CURRENT_INIT_MA, DEFAULT_RANGE_MM, DEFAULT_DENSITY_WATER);
            dataString += "," + String(rawAds) + "," + String(mv) + "," + String(ma) + "," + String(depth);
            Serial.printf("ADS A%d raw: %d | mv: %.2f mV | ma: %.3f mA | depth: %.1f mm\n", ch, rawAds, mv, ma, depth);
        }

        logSensorDataToSd(dataString);
    }

    // Non-blocking time printing
    if (currentMillis - previousTimePrintMillis >= PRINT_TIME_INTERVAL) {
        previousTimePrintMillis = currentMillis;
        printCurrentTime();
    }

    // Service web server
    // Service OTA and web server
    handleOtaUpdate();
    handleWebServerClients();
}