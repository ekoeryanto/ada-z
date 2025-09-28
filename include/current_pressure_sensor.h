#pragma once

#include <Arduino.h>

// Initialize ADS1115 at optional I2C address (default 0x48)
bool setupCurrentPressureSensor(uint8_t i2cAddress = 0x48);

// Read raw ADC value from ADS1115 channel (0-3) as signed 16-bit reading
int16_t readAdsRaw(uint8_t channel);

// Convert raw ADC reading to millivolts using library computeVolts
float adsRawToMv(int16_t raw);

// Read current in mA for given ADS channel, using shunt resistor (ohms)
// and amplifier gain (amp_gain). Defaults follow vendor sample: shunt=119Ω, amp_gain=2.0
float readAdsMa(uint8_t channel, float shunt_ohm = 119.0f, float amp_gain = 2.0f);

// Return last EMA-smoothed mA value for ADS channel
float getAdsSmoothedMa(uint8_t channel);

// Runtime setters (apply immediately)
void setAdsEmaAlpha(float a);
void setAdsNumAvg(int n);

// Compute depth in mm using sample's formula.
float computeDepthMm(float current_mA, float current_init_mA, float range_mm, float density);

// Convert 4-20mA measurement into pressure in bar (linear mapping)
float computePressureBarFromMa(float current_mA, float current_init_mA, float range_bar);

// Per-channel ADS configuration helpers (read from Preferences with defaults)
float getAdsShuntOhm(uint8_t channel);
float getAdsAmpGain(uint8_t channel);
// Per-channel ADS mode: use shunt resistor (legacy) or TP5551 current-to-voltage module
enum AdsChannelMode {
	ADS_MODE_SHUNT = 0,
	ADS_MODE_TP5551 = 1,
};

int getAdsChannelMode(uint8_t channel);
float getAdsTpScale(uint8_t channel);

// Clear ADS per-channel buffers and reset smoothed values (useful after tp_scale changes)
void clearAdsBuffers();
