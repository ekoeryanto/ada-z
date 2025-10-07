#include "voltage_pressure_sensor.h"
#include "pins_config.h" // For AI1_PIN
#include "config.h"      // For EMA_ALPHA

#include <math.h>

#include "sensor_calibration_types.h" // For SensorCalibration struct

#include "esp_adc_cal.h"
#include "sd_logger.h"
// Storage helpers provide NVS read/write helpers used to fetch runtime config like divider_mv
#include "storage_helpers.h"

// ADC calibration handle and characteristics (declared early so conversion helpers can use it)
static esp_adc_cal_characteristics_t adc_chars;

// Define the voltage pressure sensor pins
const int VOLTAGE_SENSOR_PINS[] = {AI1_PIN, AI2_PIN, AI3_PIN};
const int NUM_VOLTAGE_SENSORS = sizeof(VOLTAGE_SENSOR_PINS) / sizeof(VOLTAGE_SENSOR_PINS[0]);

// Global variables defined here
// Array to store smoothed ADC values for each sensor
static float smoothedADC[NUM_VOLTAGE_SENSORS];
// consecutive full-scale raw reads counter to detect real saturation vs transient spike
static int consecutiveSaturations[NUM_VOLTAGE_SENSORS];
// Array to store calibration data for each sensor
static SensorCalibration voltageSensorCalibrations[NUM_VOLTAGE_SENSORS];
// Runtime-configurable ADC per-read sample count
static int adcNumSamples = 3; // default
// Linear correction applied after baseline 0..10 V mapping
static float voltageLinearScale = 1.0f;
static float voltageLinearOffset = 0.0f;
static float adcDividerScale[NUM_VOLTAGE_SENSORS] = {7.6667f, 7.6667f, 7.6667f};
static const float DEFAULT_ADC_DIVIDER_SCALE[NUM_VOLTAGE_SENSORS] = {7.6667f, 7.6667f, 7.6667f};
static const char* const ADC_DIVIDER_SCALE_KEYS[NUM_VOLTAGE_SENSORS] = {
    "div_scale0",
    "div_scale1",
    "div_scale2"
};

static void loadDividerScalesFromNvs() {
    for (int i = 0; i < NUM_VOLTAGE_SENSORS; ++i) {
        float stored = loadFloatFromNVSns("adc_cfg", ADC_DIVIDER_SCALE_KEYS[i], DEFAULT_ADC_DIVIDER_SCALE[i]);
        if (!isfinite(stored) || stored <= 0.0f) {
            stored = DEFAULT_ADC_DIVIDER_SCALE[i];
        }
        adcDividerScale[i] = stored;
    }
}

float convert010V(int adc, int channelIndex) {
    if (adc < 0) adc = 0;
    if (adc > 4095) adc = 4095;

    if (channelIndex < 0 || channelIndex >= NUM_VOLTAGE_SENSORS) {
        channelIndex = 0;
    }

    float scale = adcDividerScale[channelIndex];
    if (!isfinite(scale) || scale <= 0.0f) {
        scale = DEFAULT_ADC_DIVIDER_SCALE[channelIndex];
    }

    int mv = adcRawToMv(adc);
    float adcVoltage = (float)mv / 1000.0f;
    float voltage_v = adcVoltage * scale;

    float corrected_v = voltage_v * voltageLinearScale + voltageLinearOffset;

    if (corrected_v < 0.0f) corrected_v = 0.0f;
    if (corrected_v > 10.0f) corrected_v = 10.0f;
    return corrected_v;
}

void setVoltageLinearCalibration(float scale, float offset) {
    if (!isfinite(scale) || scale == 0.0f) {
        scale = 1.0f;
    }
    if (!isfinite(offset)) {
        offset = 0.0f;
    }
    voltageLinearScale = scale;
    voltageLinearOffset = offset;
    saveFloatToNVSns("adc_cfg", "linear_scale", voltageLinearScale);
    saveFloatToNVSns("adc_cfg", "linear_offset", voltageLinearOffset);
}

void getVoltageLinearCalibration(float &scale, float &offset) {
    scale = voltageLinearScale;
    offset = voltageLinearOffset;
}

int adcRawToMv(int raw) {
    return esp_adc_cal_raw_to_voltage(raw, &adc_chars);
}

float getSmoothedVoltagePressure(int pinIndex) {
    if (pinIndex < 0 || pinIndex >= NUM_VOLTAGE_SENSORS) {
        Serial.printf("Error: Invalid pinIndex %d for voltage sensor.\n", pinIndex);
        return 0.0; // Return a default or error value
    }

    // 1. Get the current voltage reading using the complex conversion.
    float currentVoltage = convert010V((int)smoothedADC[pinIndex], pinIndex);

    // 2. Get the calibration data for the sensor.
    SensorCalibration cal = voltageSensorCalibrations[pinIndex];

    // 3. At calibration time, the raw ADC values for zero and span pressures were stored.
    //    Let's find out what the corrected voltage was at those calibration points.
    float voltageAtZeroPoint = convert010V((int)cal.zeroRawAdc, pinIndex);
    float voltageAtSpanPoint = convert010V((int)cal.spanRawAdc, pinIndex);

    // 4. Now, map the `currentVoltage` from the measured voltage range [voltageAtZeroPoint, voltageAtSpanPoint]
    //    to the desired pressure range [cal.zeroPressureValue, cal.spanPressureValue].

    // Avoid division by zero if calibration points are identical.
    if (fabs(voltageAtSpanPoint - voltageAtZeroPoint) < 0.001f) {
        return currentVoltage; // Return uncalibrated voltage if calibration is invalid.
    }

    // Perform linear interpolation (map function).
    float calibratedPressure = cal.zeroPressureValue + (currentVoltage - voltageAtZeroPoint) * 
                             (cal.spanPressureValue - cal.zeroPressureValue) / 
                             (voltageAtSpanPoint - voltageAtZeroPoint);

    return calibratedPressure;
}

// (Legacy wrappers removed) Use indexed APIs instead

#include "calibration_keys.h" // For CAL_ZERO_RAW_ADC, etc.

#include "storage_helpers.h"
// Note: we avoid using a global Preferences instance; helpers open/close per operation.


// Initialize ADC characterization (esp_adc_cal) for accurate conversions
void initAdcCalibration() {
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_11db, ADC_WIDTH_BIT_12, 1100, &adc_chars);
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        Serial.println("ADC calibrated using eFuse Vref");
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        Serial.println("ADC calibrated using Two Point");
    } else {
        Serial.println("ADC characterization used default Vref (approx)");
    }
}

void loadVoltagePressureCalibration() {
    float storedScale = loadFloatFromNVSns("adc_cfg", "linear_scale", voltageLinearScale);
    if (!isfinite(storedScale) || storedScale == 0.0f) {
        storedScale = 1.0f;
    }
    voltageLinearScale = storedScale;

    float storedOffset = loadFloatFromNVSns("adc_cfg", "linear_offset", voltageLinearOffset);
    if (!isfinite(storedOffset)) {
        storedOffset = 0.0f;
    }
    voltageLinearOffset = storedOffset;

    // Migrate legacy calibration keys if present: check for OLD_* keys and copy to short keys
    for (int i = 0; i < NUM_VOLTAGE_SENSORS; ++i) {
        String pinKey = String(VOLTAGE_SENSOR_PINS[i]);
        // If legacy keys exist but new short keys absent, migrate
        String legacyZKey = pinKey + String("_") + String(OLD_CAL_ZERO_PRESSURE_VALUE);
        String legacySKey = pinKey + String("_") + String(OLD_CAL_SPAN_PRESSURE_VALUE);
        String newZKey = pinKey + String("_") + String(CAL_ZERO_PRESSURE_VALUE);
        String newSKey = pinKey + String("_") + String(CAL_SPAN_PRESSURE_VALUE);
        // If legacy exists and new does not, copy
        float tmp;
        tmp = loadFloatFromNVSns(CAL_NAMESPACE, legacyZKey.c_str(), NAN);
        if (!isnan(tmp)) {
            // write to new key if missing or different
            float existing = loadFloatFromNVSns(CAL_NAMESPACE, newZKey.c_str(), NAN);
            if (isnan(existing)) saveFloatToNVSns(CAL_NAMESPACE, newZKey.c_str(), tmp);
        }
        tmp = loadFloatFromNVSns(CAL_NAMESPACE, legacySKey.c_str(), NAN);
        if (!isnan(tmp)) {
            float existing = loadFloatFromNVSns(CAL_NAMESPACE, newSKey.c_str(), NAN);
            if (isnan(existing)) saveFloatToNVSns(CAL_NAMESPACE, newSKey.c_str(), tmp);
        }
    }

    for (int i = 0; i < NUM_VOLTAGE_SENSORS; i++) {
        String pinKey = String(VOLTAGE_SENSOR_PINS[i]); // Create key based on pin number

        voltageSensorCalibrations[i].zeroRawAdc = loadFloatFromNVSns(CAL_NAMESPACE, (pinKey + "_" + CAL_ZERO_RAW_ADC).c_str(), 0.0f);
        voltageSensorCalibrations[i].spanRawAdc = loadFloatFromNVSns(CAL_NAMESPACE, (pinKey + "_" + CAL_SPAN_RAW_ADC).c_str(), 0.0f);
    voltageSensorCalibrations[i].zeroPressureValue = loadFloatFromNVSns(CAL_NAMESPACE, (pinKey + "_" + CAL_ZERO_PRESSURE_VALUE).c_str(), 0.0f);
    voltageSensorCalibrations[i].spanPressureValue = loadFloatFromNVSns(CAL_NAMESPACE, (pinKey + "_" + CAL_SPAN_PRESSURE_VALUE).c_str(), 0.0f);

        // If no pressure calibration was stored (both zero), apply sensible default mapping
        // that maps full ADC range to 0..10 bar for 0-10V sensors.
        if (voltageSensorCalibrations[i].zeroPressureValue == 0.0 && voltageSensorCalibrations[i].spanPressureValue == 0.0) {
            // Only apply defaults if user hasn't set any calibration values
            if (voltageSensorCalibrations[i].zeroRawAdc == 0.0 && voltageSensorCalibrations[i].spanRawAdc == 0.0) {
                voltageSensorCalibrations[i].zeroRawAdc = 0.0;
                voltageSensorCalibrations[i].spanRawAdc = 4095.0;
            }
            voltageSensorCalibrations[i].zeroPressureValue = 0.0;
            voltageSensorCalibrations[i].spanPressureValue = 10.0;
            Serial.printf("Sensor Pin %d: no calibration found, applying default 0..4095 -> 0..10 bar\n", VOLTAGE_SENSOR_PINS[i]);
        }

        // Calculate offset and scale for this sensor
        if (voltageSensorCalibrations[i].spanRawAdc - voltageSensorCalibrations[i].zeroRawAdc != 0) {
            voltageSensorCalibrations[i].scale = (voltageSensorCalibrations[i].spanPressureValue - voltageSensorCalibrations[i].zeroPressureValue) / (voltageSensorCalibrations[i].spanRawAdc - voltageSensorCalibrations[i].zeroRawAdc);
            voltageSensorCalibrations[i].offset = voltageSensorCalibrations[i].zeroPressureValue - (voltageSensorCalibrations[i].scale * voltageSensorCalibrations[i].zeroRawAdc);
        } else {
            voltageSensorCalibrations[i].scale = 1.0;
            voltageSensorCalibrations[i].offset = 0.0;
        }

        Serial.printf("Sensor Pin %d Calibration Loaded: Zero ADC=%.2f, Span ADC=%.2f, Zero Val=%.2f, Span Val=%.2f, Offset=%.4f, Scale=%.4f\n",
                      VOLTAGE_SENSOR_PINS[i],
                      voltageSensorCalibrations[i].zeroRawAdc,
                      voltageSensorCalibrations[i].spanRawAdc,
                      voltageSensorCalibrations[i].zeroPressureValue,
                      voltageSensorCalibrations[i].spanPressureValue,
                      voltageSensorCalibrations[i].offset,
                      voltageSensorCalibrations[i].scale);
    }
}

void setupVoltagePressureSensor() {
    // Initialize sensor module: load per-pin calibration and reset buffers
    loadVoltagePressureCalibration(); // Load calibration on startup
    loadDividerScalesFromNvs();

    // Seed smoothed ADCs using vendor-style averaging to match sample code
    // Read persisted value if present
    adcNumSamples = loadIntFromNVSns("adc_cfg", "num_samples", adcNumSamples);
    const int SAMPLE_DELAY_MS = 2;
    for (int i = 0; i < NUM_VOLTAGE_SENSORS; ++i) {
        int pin = VOLTAGE_SENSOR_PINS[i];
        long sum = 0;
        for (int s = 0; s < adcNumSamples; ++s) {
            sum += analogRead(pin);
            delay(SAMPLE_DELAY_MS);
        }
        int avg = (int)(sum / adcNumSamples);
        smoothedADC[i] = (float)avg;
        consecutiveSaturations[i] = (avg >= 4095) ? 1 : 0;
    }
}

// Helper: return the GPIO pin number for a given sensor index
int getVoltageSensorPin(int pinIndex) {
    if (pinIndex < 0 || pinIndex >= NUM_VOLTAGE_SENSORS) return -1;
    return VOLTAGE_SENSOR_PINS[pinIndex];
}

int findVoltageSensorIndexByPin(int pinNumber) {
    for (int i = 0; i < NUM_VOLTAGE_SENSORS; i++) {
        if (VOLTAGE_SENSOR_PINS[i] == pinNumber) return i;
    }
    return -1;
}

int getNumVoltageSensors() {
    return NUM_VOLTAGE_SENSORS;
}

void saveCalibrationForPin(int pinIndex, float zeroRawAdc, float spanRawAdc, float zeroPressureValue, float spanPressureValue) {
    if (pinIndex < 0 || pinIndex >= NUM_VOLTAGE_SENSORS) return;
    String pinKey = String(VOLTAGE_SENSOR_PINS[pinIndex]);
    saveFloatToNVSns(CAL_NAMESPACE, (pinKey + "_" + CAL_ZERO_RAW_ADC).c_str(), zeroRawAdc);
    saveFloatToNVSns(CAL_NAMESPACE, (pinKey + "_" + CAL_SPAN_RAW_ADC).c_str(), spanRawAdc);
    saveFloatToNVSns(CAL_NAMESPACE, (pinKey + "_" + CAL_ZERO_PRESSURE_VALUE).c_str(), zeroPressureValue);
    saveFloatToNVSns(CAL_NAMESPACE, (pinKey + "_" + CAL_SPAN_PRESSURE_VALUE).c_str(), spanPressureValue);

    // Verify write by reading back one of the values; if mismatch, log to SD
    float check = loadFloatFromNVSns(CAL_NAMESPACE, (pinKey + "_" + CAL_SPAN_PRESSURE_VALUE).c_str(), -9999.0f);
    if (fabs(check - spanPressureValue) > 0.001f) {
        // Defer to SD logger if available
        String msg = String("Calibration write mismatch for pin ") + String(VOLTAGE_SENSOR_PINS[pinIndex]) + String(" wrote=") + String(spanPressureValue) + String(" read=") + String(check);
        logErrorToSd(msg);
        Serial.println(msg);
    }

    // Update in-memory calibration and recompute offset/scale
    voltageSensorCalibrations[pinIndex].zeroRawAdc = zeroRawAdc;
    voltageSensorCalibrations[pinIndex].spanRawAdc = spanRawAdc;
    voltageSensorCalibrations[pinIndex].zeroPressureValue = zeroPressureValue;
    voltageSensorCalibrations[pinIndex].spanPressureValue = spanPressureValue;
    if (spanRawAdc - zeroRawAdc != 0) {
        voltageSensorCalibrations[pinIndex].scale = (spanPressureValue - zeroPressureValue) / (spanRawAdc - zeroRawAdc);
        voltageSensorCalibrations[pinIndex].offset = zeroPressureValue - (voltageSensorCalibrations[pinIndex].scale * zeroRawAdc);
    } else {
        voltageSensorCalibrations[pinIndex].scale = 1.0;
        voltageSensorCalibrations[pinIndex].offset = 0.0;
    }
}

struct SensorCalibration getCalibrationForPin(int pinIndex) {
    if (pinIndex < 0 || pinIndex >= NUM_VOLTAGE_SENSORS) {
        SensorCalibration empty = {0,0,0,0,0,1.0};
        return empty;
    }
    return voltageSensorCalibrations[pinIndex];
}


void updateVoltagePressureSensor(int pinIndex) {
    if (pinIndex < 0 || pinIndex >= NUM_VOLTAGE_SENSORS) {
        Serial.printf("Error: Invalid pinIndex %d for voltage sensor update.\n", pinIndex);
        return;
    }
    // Vendor-style averaging: take NUM_SAMPLES samples with short delay, compute average
    int NUM_SAMPLES = adcNumSamples; // runtime-configurable
    const int SAMPLE_DELAY_MS = 2;
    long sum = 0;
    int rawADC = 0;
    for (int s = 0; s < NUM_SAMPLES; ++s) {
        rawADC = analogRead(VOLTAGE_SENSOR_PINS[pinIndex]);
        sum += rawADC;
        delay(SAMPLE_DELAY_MS);
    }
    int avg = (int)(sum / NUM_SAMPLES);
    smoothedADC[pinIndex] = (float)avg;
    // Clamp to ADC range
    if (smoothedADC[pinIndex] < 0.0f) smoothedADC[pinIndex] = 0.0f;
    if (smoothedADC[pinIndex] > 4095.0f) smoothedADC[pinIndex] = 4095.0f;
    // Update saturation tracking (simple threshold)
    if (avg >= 4095) {
        consecutiveSaturations[pinIndex]++;
        if (consecutiveSaturations[pinIndex] > 1000) consecutiveSaturations[pinIndex] = 1000;
    } else {
        consecutiveSaturations[pinIndex] = 0;
    }
}

// Returns true if the pin has been saturated recently
bool isPinSaturated(int pinIndex) {
    if (pinIndex < 0 || pinIndex >= NUM_VOLTAGE_SENSORS) return false;
    return consecutiveSaturations[pinIndex] >= 3;
}

float getAdcDividerScale(int index) {
    if (index < 0 || index >= NUM_VOLTAGE_SENSORS) return DEFAULT_ADC_DIVIDER_SCALE[0];
    return adcDividerScale[index];
}

void setAdcDividerScale(int index, float scale) {
    if (index < 0 || index >= NUM_VOLTAGE_SENSORS) return;
    if (!isfinite(scale) || scale <= 0.0f) return;
    adcDividerScale[index] = scale;
    saveFloatToNVSns("adc_cfg", ADC_DIVIDER_SCALE_KEYS[index], scale);
}

const float* getAllAdcDividerScales() {
    return adcDividerScale;
}

// (Legacy wrappers removed) Use indexed APIs instead

// Accessor for raw smoothed ADC of a specific sensor
float getSmoothedADC(int pinIndex) {
    if (pinIndex < 0 || pinIndex >= NUM_VOLTAGE_SENSORS) return 0.0;
    return smoothedADC[pinIndex];
}

// Runtime getters/setters for ADC per-read sample count
int getAdcNumSamples() {
    return loadIntFromNVSns("adc_cfg", "num_samples", adcNumSamples);
}

void setAdcNumSamples(int n) {
    if (n < 1) return;
    saveIntToNVSns("adc_cfg", "num_samples", n);
    // Update runtime variable
    adcNumSamples = n;
}
