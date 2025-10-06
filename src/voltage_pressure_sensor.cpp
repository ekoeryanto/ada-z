#include "voltage_pressure_sensor.h"
#include "pins_config.h" // For AI1_PIN
#include "config.h"      // For EMA_ALPHA

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

float convert010V(int adc) {
    // Convert raw ADC to millivolts using esp_adc_cal for best accuracy
    int mv = esp_adc_cal_raw_to_voltage(adc, &adc_chars); // mV at ADC input (approx 0..3300)

    // Allow divider voltage to be configured at runtime via NVS key adc_cfg/divider_mv
    float divider_mv = loadFloatFromNVSns("adc_cfg", "divider_mv", 3300.0f);
    if (divider_mv <= 0.0f) divider_mv = 3300.0f;

    // Vadc is voltage seen by ADC (in volts)
    float Vadc = (float)mv / 1000.0f; // ~0..3.3

    // Map measured ADC mV back to original 0..10V input using divider ratio
    // Volt_input = mv * (10.0 / divider_mv)
    float voltage_v = (float)mv * (10.0f / divider_mv);

    // Apply vendor-provided piecewise linear correction to improve linearity
    // based on measured Vadc ranges in the sample code
    float Vcal = 0.0f;

    // Detect true 0 and true saturation robustly (avoid exact equality checks)
    if (mv <= 0 || Vadc <= 0.0001f) {
        Vcal = 0.0f;
    } else {
        // Consider ADC near full-scale as saturation: use small margin below divider_mv
        const int SAT_MARGIN_MV = 4; // 4 mV margin
        if (mv >= (int)(divider_mv) - SAT_MARGIN_MV || adc >= 4095) {
            Vcal = 10.0f;
        } else if (Vadc > 0.01f && Vadc <= 0.96f) {
            Vcal = 1.0345f * voltage_v + 0.2897f;
        } else if (Vadc > 0.96f && Vadc <= 1.52f) {
            Vcal = 1.0029f * voltage_v + 0.3814f;
        } else /* Vadc > 1.52 */ {
            Vcal = 0.932f * voltage_v + 0.7083f;
        }
    }

    // Clamp final value to 0..10 V
    if (Vcal < 0.0f) Vcal = 0.0f;
    if (Vcal > 10.0f) Vcal = 10.0f;
    return Vcal;
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
    float currentVoltage = convert010V((int)smoothedADC[pinIndex]);

    // 2. Get the calibration data for the sensor.
    SensorCalibration cal = voltageSensorCalibrations[pinIndex];

    // 3. At calibration time, the raw ADC values for zero and span pressures were stored.
    //    Let's find out what the corrected voltage was at those calibration points.
    float voltageAtZeroPoint = convert010V((int)cal.zeroRawAdc);
    float voltageAtSpanPoint = convert010V((int)cal.spanRawAdc);

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
