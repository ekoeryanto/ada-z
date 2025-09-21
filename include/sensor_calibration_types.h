#ifndef SENSOR_CALIBRATION_TYPES_H
#define SENSOR_CALIBRATION_TYPES_H

struct SensorCalibration {
    float zeroRawAdc;        // Raw ADC value at zero pressure
    float spanRawAdc;        // Raw ADC value at span pressure
    float zeroPressureValue; // Actual pressure value at zero point
    float spanPressureValue; // Actual pressure value at span point
    float offset;            // Calculated offset for calibration
    float scale;             // Calculated scale for calibration
};

#endif // SENSOR_CALIBRATION_TYPES_H
