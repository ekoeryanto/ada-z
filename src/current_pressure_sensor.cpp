#include "current_pressure_sensor.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

static Adafruit_ADS1115 ads; // 16-bit ADC
static uint8_t adsAddress = 0x48;
static bool adsInitialized = false;

bool setupCurrentPressureSensor(uint8_t i2cAddress) {
    adsAddress = i2cAddress;
    Wire.begin();
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
    Serial.print("ADS1115 initialized at 0x");
    Serial.println(String(adsAddress, HEX));
    return true;
}

int16_t readAdsRaw(uint8_t channel) {
    if (!adsInitialized) return 0;
    if (channel > 3) return 0;
    int16_t val = ads.readADC_SingleEnded(channel);
    return val;
}

float adsRawToMv(int16_t raw) {
    if (!adsInitialized) return 0.0f;
    // Use library helper which considers configured gain
    float volts = ads.computeVolts(raw); // volts
    return volts * 1000.0f; // mV
}

// Read current in mA using shunt resistor and amplifier gain
float readAdsMa(uint8_t channel, float shunt_ohm, float amp_gain) {
    if (!adsInitialized) return 0.0f;
    if (channel > 3) return 0.0f;
    int16_t raw = readAdsRaw(channel);
    float mv = adsRawToMv(raw);
    if (shunt_ohm <= 0.0f) return 0.0f;
    // Vendor example: current = (mv / shunt_ohm) / amp_gain
    return (mv / shunt_ohm) / amp_gain;
}

// Compute depth in mm using sample's formula.
// depth = (current_mA - current_init_mA) * (range_mm / density / 16.0)
float computeDepthMm(float current_mA, float current_init_mA, float range_mm, float density) {
    float depth = (current_mA - current_init_mA) * (range_mm / density / 16.0f);
    if (depth < 0.0f) return 0.0f;
    return depth;
}

// Preferences-backed helpers for per-channel shunt and amp gain
#include <Preferences.h>

float getAdsShuntOhm(uint8_t channel) {
    Preferences p;
    p.begin("ads_cfg", true);
    char key[16];
    snprintf(key, sizeof(key), "shunt_%d", channel);
    float v = p.getFloat(key, DEFAULT_SHUNT_OHM);
    p.end();
    return v;
}

float getAdsAmpGain(uint8_t channel) {
    Preferences p;
    p.begin("ads_cfg", true);
    char key[16];
    snprintf(key, sizeof(key), "amp_%d", channel);
    float v = p.getFloat(key, DEFAULT_AMP_GAIN);
    p.end();
    return v;
}
