#include "current_pressure_sensor.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include "i2c_helpers.h"
// Preferences helpers
#include "calibration_keys.h"
#include "storage_helpers.h"

static Adafruit_ADS1115 ads; // 16-bit ADC
static uint8_t adsAddress = 0x48;
static bool adsInitialized = false;
// EMA smoothing for ADS channels to reduce jitter from noisy current loop
static float adsSmoothedMa[4] = {0.0f, 0.0f, 0.0f, 0.0f};
// runtime-configurable smoothing parameters (stored in NVS under "ads_cfg")
static float adsEmaAlpha = 0.1f; // default smoothing factor
static int adsNumAvg = 5; // number of readings to median/average (default)
// Circular buffer for median filter per ADS channel
static const int ADS_MAX_BUF = 21; // max allowed buffer length
static int16_t adsBuf[4][ADS_MAX_BUF];
static int adsBufIdx[4] = {0,0,0,0};
static int adsBufCount[4] = {0,0,0,0};

bool setupCurrentPressureSensor(uint8_t i2cAddress) {
    adsAddress = i2cAddress;
    // ensure I2C initialized by central helper
    initI2C();
    if (!ads.begin(adsAddress)) {
        Serial.print("Failed to initialize ADS1115 at 0x");
        Serial.println(String(adsAddress, HEX));
        adsInitialized = false;
        return false;
    }
    // Use vendor sample default: GAIN_TWOTHIRDS => +/-6.144V (ADS1115, 1 bit = 0.1875mV)
    // This provides the largest input range and matches the Test_AI_4-20mA sample.
    ads.setGain(GAIN_TWOTHIRDS);
    adsInitialized = true;
    // Initialize smoothing buffers and load runtime params from Preferences
    for (int ch = 0; ch < 4; ++ch) {
        adsSmoothedMa[ch] = 0.0f;
        adsBufIdx[ch] = 0;
        adsBufCount[ch] = 0;
        for (int i = 0; i < ADS_MAX_BUF; ++i) adsBuf[ch][i] = 0;
    }
    // Load smoothing params from NVS if present
    adsEmaAlpha = loadFloatFromNVSns("ads_cfg", "ema_alpha", adsEmaAlpha);
    adsNumAvg = loadIntFromNVSns("ads_cfg", "num_avg", adsNumAvg);
    Serial.print("ADS1115 initialized at 0x");
    Serial.println(String(adsAddress, HEX));
    return true;
}

int16_t readAdsRaw(uint8_t channel) {
    if (!adsInitialized) return 0;
    if (channel > 3) return 0;
    // Read one sample (we'll push into buffer and compute median outside)
    int16_t v = ads.readADC_SingleEnded(channel);
    return v;
}

float adsRawToMv(int16_t raw) {
    if (!adsInitialized) return 0.0f;
    // Prevent tiny negative raw readings (can occur when input floats slightly below ground)
    if (raw < 0) raw = 0;
    // Use library helper which considers configured gain
    float volts = ads.computeVolts(raw); // volts
    float mv = volts * 1000.0f; // mV
    if (mv < 0.0f) mv = 0.0f;
    return mv;
}

// Read current in mA using shunt resistor and amplifier gain
float readAdsMa(uint8_t channel, float shunt_ohm, float amp_gain) {
    if (!adsInitialized) return 0.0f;
    if (channel > 3) return 0.0f;
    int16_t raw = readAdsRaw(channel);
    float mv = adsRawToMv(raw);
    // Determine per-channel mode (shunt vs TP5551)
    char mkey[16]; snprintf(mkey, sizeof(mkey), "mode_%d", channel);
    int mode = loadIntFromNVSns("ads_cfg", mkey, ADS_MODE_TP5551);

        if (mode == ADS_MODE_TP5551) {
        // TP5551 outputs a voltage proportional to current. Use per-channel tp_scale
        // which is mV per mA. Default estimated from previous shunt+amp default: 119 * 2 = 238 mV/mA
    char skey[16]; snprintf(skey, sizeof(skey), "tp_scale_%d", channel);
    float tp_scale = loadFloatFromNVSns(CAL_NAMESPACE, skey, 238.0f);
        if (tp_scale <= 0.0f) return 0.0f;
    float m = mv / tp_scale; // mA
    if (m < 0.0f) m = 0.0f; // avoid tiny negative currents from floating inputs
        // Push into median buffer
    int idx = adsBufIdx[channel] % ADS_MAX_BUF;
    adsBuf[channel][idx] = (int16_t)round(m * 1000.0f); // store as fixed mA*1000
        adsBufIdx[channel] = (adsBufIdx[channel] + 1) % ADS_MAX_BUF;
        if (adsBufCount[channel] < adsNumAvg) adsBufCount[channel]++;

        // Build temp array for median from last adsNumAvg entries
        int n = adsBufCount[channel];
        int tmp[ADS_MAX_BUF];
        int start = (adsBufIdx[channel] - n + ADS_MAX_BUF) % ADS_MAX_BUF;
        for (int i = 0; i < n; ++i) tmp[i] = adsBuf[channel][(start + i) % ADS_MAX_BUF];
        // sort small array
        for (int i = 0; i < n-1; ++i) for (int j = i+1; j < n; ++j) if (tmp[j] < tmp[i]) { int t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t; }
        int median = tmp[n/2];
        float median_ma = ((float)median) / 1000.0f;
        // EMA smoothing on median
        adsSmoothedMa[channel] = (adsEmaAlpha * median_ma) + ((1.0f - adsEmaAlpha) * adsSmoothedMa[channel]);
        return adsSmoothedMa[channel];
    } else {
        if (shunt_ohm <= 0.0f) return 0.0f;
        // Vendor example: current = (mv / shunt_ohm) / amp_gain
        float m = (mv / shunt_ohm) / amp_gain;
        // push into buffer as fixed value
        int idx = adsBufIdx[channel] % ADS_MAX_BUF;
        adsBuf[channel][idx] = (int16_t)round(m * 1000.0f);
        adsBufIdx[channel] = (adsBufIdx[channel] + 1) % ADS_MAX_BUF;
        if (adsBufCount[channel] < adsNumAvg) adsBufCount[channel]++;

        int n = adsBufCount[channel];
        int tmp[ADS_MAX_BUF];
        int start = (adsBufIdx[channel] - n + ADS_MAX_BUF) % ADS_MAX_BUF;
        for (int i = 0; i < n; ++i) tmp[i] = adsBuf[channel][(start + i) % ADS_MAX_BUF];
        for (int i = 0; i < n-1; ++i) for (int j = i+1; j < n; ++j) if (tmp[j] < tmp[i]) { int t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t; }
        int median = tmp[n/2];
        float median_ma = ((float)median) / 1000.0f;
        adsSmoothedMa[channel] = (adsEmaAlpha * median_ma) + ((1.0f - adsEmaAlpha) * adsSmoothedMa[channel]);
        return adsSmoothedMa[channel];
    }
}

// Accessor: get the last smoothed mA value for an ADS channel
float getAdsSmoothedMa(uint8_t channel) {
    if (channel > 3) return 0.0f;
    return adsSmoothedMa[channel];
}

void setAdsEmaAlpha(float a) {
    if (a <= 0.0f) return;
    if (a > 1.0f) a = 1.0f;
    adsEmaAlpha = a;
}

void setAdsNumAvg(int n) {
    if (n < 1) n = 1;
    if (n > ADS_MAX_BUF) n = ADS_MAX_BUF;
    adsNumAvg = n;
}

// Compute depth in mm using sample's formula.
// depth = (current_mA - current_init_mA) * (range_mm / density / 16.0)
float computeDepthMm(float current_mA, float current_init_mA, float range_mm, float density) {
    float depth = (current_mA - current_init_mA) * (range_mm / density / 16.0f);
    if (depth < 0.0f) return 0.0f;
    return depth;
}

// Convert current (mA) from a 4-20mA transmitter into pressure (bar)
// and depth (mm). Assumptions:
// - Transmitter output: 4 mA -> 0 bar, 20 mA -> full-scale (DEFAULT_RANGE_BAR)
// - Linear mapping between current and pressure
// - `current_init_mA` is the 4 mA zero point (defaults to 4.0)
// Use computePressureBarFromMa() to get bar; then computeDepthMm() for mm.
float computePressureBarFromMa(float current_mA, float current_init_mA, float range_bar) {
    if (current_mA <= current_init_mA) return 0.0f;
    // 4-20mA spans 16 mA for the full scale
    float span_ma = 16.0f;
    float p = (current_mA - current_init_mA) * (range_bar / span_ma);
    if (p < 0.0f) return 0.0f;
    return p;
}

// NVS-backed helpers for per-channel shunt and amp gain
float getAdsShuntOhm(uint8_t channel) {
    char key[16];
    snprintf(key, sizeof(key), "shunt_%d", channel);
    return loadFloatFromNVSns("ads_cfg", key, DEFAULT_SHUNT_OHM);
}

float getAdsAmpGain(uint8_t channel) {
    char key[16];
    snprintf(key, sizeof(key), "amp_%d", channel);
    return loadFloatFromNVSns("ads_cfg", key, DEFAULT_AMP_GAIN);
}

int getAdsChannelMode(uint8_t channel) {
    char key[16]; snprintf(key, sizeof(key), "mode_%d", channel);
    return loadIntFromNVSns("ads_cfg", key, ADS_MODE_TP5551);
}

float getAdsTpScale(uint8_t channel) {
    char key[16]; snprintf(key, sizeof(key), "tp_scale_%d", channel);
    return loadFloatFromNVSns(CAL_NAMESPACE, key, 238.0f);
}

// Clear ADS per-channel buffers and reset smoothed values (useful after tp_scale changes)
void clearAdsBuffers() {
    for (int ch = 0; ch < 4; ++ch) {
        adsSmoothedMa[ch] = 0.0f;
        adsBufIdx[ch] = 0;
        adsBufCount[ch] = 0;
        for (int i = 0; i < ADS_MAX_BUF; ++i) adsBuf[ch][i] = 0;
    }
    // Reseed smoothed values from current readings to avoid long ramp-up
    for (int ch = 0; ch < 4; ++ch) {
        // perform a single read and push through the same path by calling readAdsMa
        // using stored defaults; this will populate buffers with one entry each
        float dummy = readAdsMa(ch, getAdsShuntOhm(ch), getAdsAmpGain(ch));
        (void)dummy;
    }
}
