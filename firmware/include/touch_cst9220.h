#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "config.h"

class TouchCST9220 {
public:
    TouchCST9220();
    bool init();
    bool readTouch(int16_t *x, int16_t *y);
    void setRotation(uint8_t rotation);

    static void touchReadCallback(lv_indev_drv_t *drv, lv_indev_data_t *data);

private:
    uint8_t _rotation;
    int16_t _lastX;
    int16_t _lastY;
    bool _isPressed;
    lv_indev_drv_t _indevDrv;
    lv_indev_t *_indev;
};

extern TouchCST9220 touch;
