#include "imu_qmi8658.h"
#include "touch_cst9220.h"
#include <Wire.h>
#include "esp_log.h"
#include <math.h>

static const char *TAG = "QMI8658";

IMUQMI8658 imu;

#define QMI8658_WHO_AM_I   0x00
#define QMI8658_CTRL1      0x02
#define QMI8658_CTRL2      0x03
#define QMI8658_CTRL3      0x04
#define QMI8658_CTRL7      0x08
#define QMI8658_AX_L       0x35

IMUQMI8658::IMUQMI8658()
    : _currentOrientation(DeviceOrientation::ROTATION_0),
      _candidateOrientation(DeviceOrientation::ROTATION_0),
      _candidateStartTime(0) {}

bool IMUQMI8658::init() {
    ESP_LOGI(TAG, "Initializing QMI8658 IMU...");

    // Check device ID
    Wire.beginTransmission(IMU_I2C_ADDR);
    Wire.write(QMI8658_WHO_AM_I);
    if (Wire.endTransmission() != 0) {
        ESP_LOGE(TAG, "QMI8658 not detected on I2C bus");
        return false;
    }

    Wire.requestFrom((uint8_t)IMU_I2C_ADDR, (uint8_t)1);
    uint8_t chipId = Wire.read();
    ESP_LOGI(TAG, "QMI8658 Chip ID: 0x%02X", chipId);

    // Enable Accelerometer and Gyroscope
    Wire.beginTransmission(IMU_I2C_ADDR);
    Wire.write(QMI8658_CTRL1);
    Wire.write(0x60); // Auto-increment address
    Wire.endTransmission();

    // Enable Accelerometer (8g range, 100Hz ODR)
    Wire.beginTransmission(IMU_I2C_ADDR);
    Wire.write(QMI8658_CTRL2);
    Wire.write(0x23);
    Wire.endTransmission();

    // Enable Gyroscope (512dps range, 100Hz ODR)
    Wire.beginTransmission(IMU_I2C_ADDR);
    Wire.write(QMI8658_CTRL3);
    Wire.write(0x53);
    Wire.endTransmission();

    // Enable Accel & Gyro sensors
    Wire.beginTransmission(IMU_I2C_ADDR);
    Wire.write(QMI8658_CTRL7);
    Wire.write(0x03);
    Wire.endTransmission();

    ESP_LOGI(TAG, "QMI8658 initialized.");
    return true;
}

bool IMUQMI8658::readAccel(float *ax, float *ay, float *az) {
    Wire.beginTransmission(IMU_I2C_ADDR);
    Wire.write(QMI8658_AX_L);
    if (Wire.endTransmission() != 0) {
        return false;
    }

    if (Wire.requestFrom((uint8_t)IMU_I2C_ADDR, (uint8_t)6) != 6) {
        return false;
    }

    int16_t rawX = (int16_t)(Wire.read() | (Wire.read() << 8));
    int16_t rawY = (int16_t)(Wire.read() | (Wire.read() << 8));
    int16_t rawZ = (int16_t)(Wire.read() | (Wire.read() << 8));

    // Convert to g (assuming ±8g scale: 4096 LSB/g)
    *ax = (float)rawX / 4096.0f;
    *ay = (float)rawY / 4096.0f;
    *az = (float)rawZ / 4096.0f;

    return true;
}

DeviceOrientation IMUQMI8658::calculateOrientation(float ax, float ay, float az) {
    // If predominantly flat (Z vector high), keep current orientation
    if (fabsf(az) > 0.85f) {
        return _currentOrientation;
    }

    if (fabsf(ax) > fabsf(ay)) {
        if (ax > 0.5f) {
            return DeviceOrientation::ROTATION_270;
        } else if (ax < -0.5f) {
            return DeviceOrientation::ROTATION_90;
        }
    } else {
        if (ay > 0.5f) {
            return DeviceOrientation::ROTATION_180;
        } else if (ay < -0.5f) {
            return DeviceOrientation::ROTATION_0;
        }
    }

    return _currentOrientation;
}

void IMUQMI8658::update() {
    float ax = 0, ay = 0, az = 0;
    if (!readAccel(&ax, &ay, &az)) {
        return;
    }

    DeviceOrientation detected = calculateOrientation(ax, ay, az);
    uint32_t now = millis();

    if (detected != _currentOrientation) {
        if (detected != _candidateOrientation) {
            _candidateOrientation = detected;
            _candidateStartTime = now;
        } else if (now - _candidateStartTime >= IMU_DEBOUNCE_MS) {
            _currentOrientation = _candidateOrientation;
            ESP_LOGI(TAG, "Orientation stabilized to: %d", (int)_currentOrientation);

            // Update display rotation and touch mapping
            lv_disp_t *disp = lv_disp_get_default();
            if (disp) {
                lv_disp_set_rotation(disp, (lv_disp_rot_t)_currentOrientation);
            }
            touch.setRotation((uint8_t)_currentOrientation);
        }
    } else {
        _candidateOrientation = _currentOrientation;
        _candidateStartTime = now;
    }
}
