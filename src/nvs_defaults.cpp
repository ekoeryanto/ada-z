#include "nvs_defaults.h"
#include "storage_helpers.h"
#include "calibration_keys.h"
#include "current_pressure_sensor.h"
#include "pins_config.h"
#include <Preferences.h>

// Default constants (keep in sync with other modules)
static const float DEFAULT_TP_SCALE = 238.0f;
static const float DEFAULT_SHUNT_OHM = 119.0f; // matches DEFAULT_SHUNT_OHM in code
static const float DEFAULT_AMP_GAIN = 2.0f;    // matches DEFAULT_AMP_GAIN
static const float DEFAULT_DIVIDER_MV = 3300.0f;
static const float DEFAULT_ADS_EMA_ALPHA = 0.1f;
static const int DEFAULT_ADS_NUM_AVG = 5;
static const int DEFAULT_ADC_SAMPLES_PER_SENSOR = 4;

namespace {

template<typename PutFn, typename ValueType>
void ensureKey(const char* ns, const char* key, const ValueType &defaultValue, PutFn putFn) {
    Preferences prefs;
    if (!prefs.begin(ns, false)) {
        return;
    }
    if (!prefs.isKey(key)) {
        putFn(prefs, key, defaultValue);
    }
    prefs.end();
}

void ensureFloat(const char* ns, const char* key, float defaultValue) {
    ensureKey(ns, key, defaultValue, [](Preferences &p, const char* k, float v) { p.putFloat(k, v); });
}

void ensureInt(const char* ns, const char* key, int defaultValue) {
    ensureKey(ns, key, defaultValue, [](Preferences &p, const char* k, int v) { p.putInt(k, v); });
}

bool namespaceHasKey(const char* ns, const char* key) {
    Preferences prefs;
    if (!prefs.begin(ns, true)) {
        return false;
    }
    bool exists = prefs.isKey(key);
    prefs.end();
    return exists;
}

} // namespace

void ensureNvsDefaults() {
    // CAL_NAMESPACE: tp_scale_0..3 (but only 0..1 used in this board; create for 0..3 to be safe)
    for (int ch = 0; ch < 4; ++ch) {
        char key[24];
        snprintf(key, sizeof(key), "tp_scale_%d", ch);
        ensureFloat(CAL_NAMESPACE, key, DEFAULT_TP_SCALE);
    }

    // ads_cfg: shunt_#, amp_#, ema_alpha, num_avg, mode_#
    for (int ch = 0; ch < 4; ++ch) {
        char key[24];
        snprintf(key, sizeof(key), "shunt_%d", ch);
        ensureFloat("ads_cfg", key, DEFAULT_SHUNT_OHM);
        snprintf(key, sizeof(key), "amp_%d", ch);
        ensureFloat("ads_cfg", key, DEFAULT_AMP_GAIN);
        snprintf(key, sizeof(key), "mode_%d", ch);
        ensureInt("ads_cfg", key, ADS_MODE_TP5551);
    }
    ensureFloat("ads_cfg", "ema_alpha", DEFAULT_ADS_EMA_ALPHA);
    ensureInt("ads_cfg", "num_avg", DEFAULT_ADS_NUM_AVG);

    // adc_cfg: divider_mv, samples_per_sensor (stored under short key 'sps')
    ensureFloat("adc_cfg", "divider_mv", DEFAULT_DIVIDER_MV);

    // Migrate legacy key if present
    if (namespaceHasKey("adc_cfg", "samples_per_sensor") && !namespaceHasKey("adc_cfg", "sps")) {
        int legacy = loadIntFromNVSns("adc_cfg", "samples_per_sensor", DEFAULT_ADC_SAMPLES_PER_SENSOR);
        ensureInt("adc_cfg", "sps", legacy);
    } else {
        ensureInt("adc_cfg", "sps", DEFAULT_ADC_SAMPLES_PER_SENSOR);
    }

    // adc_cfg per-channel divider scale defaults and linear correction
    const float DEFAULT_ADC_DIVIDER_SCALE[3] = {7.6667f, 7.6667f, 7.6667f};
    const char* const DIV_KEYS[3] = {"div_scale0", "div_scale1", "div_scale2"};
    for (int i = 0; i < 3; ++i) {
        ensureFloat("adc_cfg", DIV_KEYS[i], DEFAULT_ADC_DIVIDER_SCALE[i]);
    }
    ensureFloat("adc_cfg", "linear_scale", 1.0f);
    ensureFloat("adc_cfg", "linear_offset", 0.0f);

    // Calibration defaults per voltage sensor pin so first boot avoids NOT_FOUND noise
    const int CAL_PINS[] = {AI1_PIN, AI2_PIN, AI3_PIN};
    for (size_t i = 0; i < sizeof(CAL_PINS) / sizeof(CAL_PINS[0]); ++i) {
        String pinKey = String(CAL_PINS[i]);
        ensureFloat(CAL_NAMESPACE, (pinKey + "_" + CAL_ZERO_RAW_ADC).c_str(), 0.0f);
        ensureFloat(CAL_NAMESPACE, (pinKey + "_" + CAL_SPAN_RAW_ADC).c_str(), 0.0f);
        ensureFloat(CAL_NAMESPACE, (pinKey + "_" + CAL_ZERO_PRESSURE_VALUE).c_str(), 0.0f);
        ensureFloat(CAL_NAMESPACE, (pinKey + "_" + CAL_SPAN_PRESSURE_VALUE).c_str(), 0.0f);
    }

    // Other small defaults can be added here if needed in future
}
