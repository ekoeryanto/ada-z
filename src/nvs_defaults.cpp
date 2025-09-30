#include "nvs_defaults.h"
#include "storage_helpers.h"
#include "calibration_keys.h"
#include "current_pressure_sensor.h"
#include <Preferences.h>

// Default constants (keep in sync with other modules)
static const float DEFAULT_TP_SCALE = 238.0f;
static const float DEFAULT_SHUNT_OHM = 119.0f; // matches DEFAULT_SHUNT_OHM in code
static const float DEFAULT_AMP_GAIN = 2.0f;    // matches DEFAULT_AMP_GAIN
static const float DEFAULT_DIVIDER_MV = 3300.0f;
static const float DEFAULT_ADS_EMA_ALPHA = 0.1f;
static const int DEFAULT_ADS_NUM_AVG = 5;
static const int DEFAULT_ADC_SAMPLES_PER_SENSOR = 4;

// Helper: check whether a key exists in given namespace
static bool nvsKeyExists(const char* ns, const char* key) {
    Preferences p;
    p.begin(ns, true); // read-only begin
    size_t len = p.getBytesLength(key);
    p.end();
    return len > 0 || (len == 0 && p.getString(key, String()).length() > 0);
}

void ensureNvsDefaults() {
    // CAL_NAMESPACE: tp_scale_0..3 (but only 0..1 used in this board; create for 0..3 to be safe)
    for (int ch = 0; ch < 4; ++ch) {
        char key[24]; snprintf(key, sizeof(key), "tp_scale_%d", ch);
        // If key missing, write default
        if (!nvsKeyExists(CAL_NAMESPACE, key)) {
            saveFloatToNVSns(CAL_NAMESPACE, key, DEFAULT_TP_SCALE);
        }
    }

    // ads_cfg: shunt_#, amp_#, ema_alpha, num_avg, mode_#
    for (int ch = 0; ch < 4; ++ch) {
        char key[24];
        snprintf(key, sizeof(key), "shunt_%d", ch);
        if (!nvsKeyExists("ads_cfg", key)) saveFloatToNVSns("ads_cfg", key, DEFAULT_SHUNT_OHM);
        snprintf(key, sizeof(key), "amp_%d", ch);
        if (!nvsKeyExists("ads_cfg", key)) saveFloatToNVSns("ads_cfg", key, DEFAULT_AMP_GAIN);
        snprintf(key, sizeof(key), "mode_%d", ch);
        if (!nvsKeyExists("ads_cfg", key)) saveIntToNVSns("ads_cfg", key, ADS_MODE_TP5551);
    }
    if (!nvsKeyExists("ads_cfg", "ema_alpha")) saveFloatToNVSns("ads_cfg", "ema_alpha", DEFAULT_ADS_EMA_ALPHA);
    if (!nvsKeyExists("ads_cfg", "num_avg")) saveIntToNVSns("ads_cfg", "num_avg", DEFAULT_ADS_NUM_AVG);

    // adc_cfg: divider_mv, samples_per_sensor
    if (!nvsKeyExists("adc_cfg", "divider_mv")) saveFloatToNVSns("adc_cfg", "divider_mv", DEFAULT_DIVIDER_MV);
    if (!nvsKeyExists("adc_cfg", "samples_per_sensor")) saveIntToNVSns("adc_cfg", "samples_per_sensor", DEFAULT_ADC_SAMPLES_PER_SENSOR);

    // Other small defaults can be added here if needed in future
}
