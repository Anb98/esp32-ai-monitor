#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "config.h"

enum class DeviceOrientation : uint8_t {
    ROTATION_0 = 0,   // Normal
    ROTATION_90 = 1,  // 90 deg clockwise
    ROTATION_180 = 2, // Inverted
    ROTATION_270 = 3  // 270 deg clockwise
};

class IMUQMI8658 {
public:
    IMUQMI8658();
    bool init();
    void update();
    DeviceOrientation getCurrentOrientation() const { return _currentOrientation; }

private:
    bool readAccel(float *ax, float *ay, float *az);
    DeviceOrientation calculateOrientation(float ax, float ay, float az);

    DeviceOrientation _currentOrientation;
    DeviceOrientation _candidateOrientation;
    uint32_t _candidateStartTime;
};

extern IMUQMI8658 imu;
