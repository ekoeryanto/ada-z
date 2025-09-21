#include "voltage_pressure_sensor.h"
#include "pins_config.h" // For AI1_PIN
#include "config.h"      // For EMA_ALPHA

#include "sensor_calibration_types.h" // For SensorCalibration struct

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

float convert010V(int adc) {
    float Vadc = adc / 4095.0f * 3.3f;
    float Volt = Vadc / 3.3f * 10.0f;
    float Vcal = 0.0f;

    if (Vadc <= 0.0f) {
        Vcal = 0.0f;
    } else if (Vadc > 0.01f && Vadc <= 0.96f) {
        Vcal = 1.0345f * Volt + 0.2897f;
    } else if (Vadc > 0.96f && Vadc <= 1.52f) {
        Vcal = 1.0029f * Volt + 0.3814f;
    } else if (Vadc > 1.52f && Vadc < 3.3f) {
        Vcal = 0.932f * Volt + 0.7083f;
    } else { // covers Vadc >= 3.3f and any unexpected values
        Vcal = 10.0f;
    }

    return Vcal;
}

float getSmoothedVoltagePressure(int pinIndex) {
    if (pinIndex < 0 || pinIndex >= NUM_VOLTAGE_SENSORS) {
        Serial.printf("Error: Invalid pinIndex %d for voltage sensor.\n", pinIndex);
        return 0.0; // Return a default or error value
    }

    // Apply calibration: calibration was stored as pressure vs RAW ADC values
    // Use the smoothed ADC (raw ADC units) when applying the stored scale/offset
    float rawAdc = round(smoothedADC[pinIndex]);

    // If calibration is the default mapping (0..4095 -> 0..10 bar) prefer converting via
    // convert010V so the reported pressure exactly matches the voltage conversion curve.
    SensorCalibration cal = voltageSensorCalibrations[pinIndex];
    if (cal.zeroRawAdc == 0.0f && cal.spanRawAdc == 4095.0f && cal.zeroPressureValue == 0.0f && cal.spanPressureValue == 10.0f) {
        // convert010V expects an ADC value -> returns volts (0-10V); treat volts as bar
        float volts = convert010V((int)rawAdc);
        return volts; // report volts as bar for default mapping
    }

    // Apply linear calibration for the specific sensor: pressure = rawAdc * scale + offset
    float calibratedPressure = (rawAdc * cal.scale) + cal.offset;

    return calibratedPressure;
}

// (Legacy wrappers removed) Use indexed APIs instead

#include <Preferences.h> // Include Preferences for loading calibration

extern Preferences preferences; // Declare extern for Preferences object

#include "calibration_keys.h" // For CAL_ZERO_RAW_ADC, etc.



void loadVoltagePressureCalibration() {
    preferences.begin(CAL_NAMESPACE, true); // Use the same namespace as web_api.cpp

    for (int i = 0; i < NUM_VOLTAGE_SENSORS; i++) {
        String pinKey = String(VOLTAGE_SENSOR_PINS[i]); // Create key based on pin number

        voltageSensorCalibrations[i].zeroRawAdc = preferences.getFloat((pinKey + "_" + CAL_ZERO_RAW_ADC).c_str(), 0.0);
        voltageSensorCalibrations[i].spanRawAdc = preferences.getFloat((pinKey + "_" + CAL_SPAN_RAW_ADC).c_str(), 0.0);
        voltageSensorCalibrations[i].zeroPressureValue = preferences.getFloat((pinKey + "_" + CAL_ZERO_PRESSURE_VALUE).c_str(), 0.0);
        voltageSensorCalibrations[i].spanPressureValue = preferences.getFloat((pinKey + "_" + CAL_SPAN_PRESSURE_VALUE).c_str(), 0.0);

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
    preferences.end();
}

void setupVoltagePressureSensor() {
    // Initialize sensor module: load per-pin calibration and reset buffers
    loadVoltagePressureCalibration(); // Load calibration on startup

    // Seed smoothed ADCs using vendor-style averaging to match sample code
    const int NUM_SAMPLES = 20;
    const int SAMPLE_DELAY_MS = 5;
    for (int i = 0; i < NUM_VOLTAGE_SENSORS; ++i) {
        int pin = VOLTAGE_SENSOR_PINS[i];
        long sum = 0;
        for (int s = 0; s < NUM_SAMPLES; ++s) {
            sum += analogRead(pin);
            delay(SAMPLE_DELAY_MS);
        }
        int avg = (int)(sum / NUM_SAMPLES);
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
    preferences.begin(CAL_NAMESPACE, false);
    preferences.putFloat((pinKey + "_" + CAL_ZERO_RAW_ADC).c_str(), zeroRawAdc);
    preferences.putFloat((pinKey + "_" + CAL_SPAN_RAW_ADC).c_str(), spanRawAdc);
    preferences.putFloat((pinKey + "_" + CAL_ZERO_PRESSURE_VALUE).c_str(), zeroPressureValue);
    preferences.putFloat((pinKey + "_" + CAL_SPAN_PRESSURE_VALUE).c_str(), spanPressureValue);
    preferences.end();

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
    const int NUM_SAMPLES = 20;
    const int SAMPLE_DELAY_MS = 5;
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
