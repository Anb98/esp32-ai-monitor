#include "touch_cst9220.h"
#include <Wire.h>
#include "esp_log.h"

static const char *TAG = "CST9220";

TouchCST9220 touch;

TouchCST9220::TouchCST9220() : _rotation(0), _lastX(0), _lastY(0), _isPressed(false), _indev(nullptr) {}

bool TouchCST9220::init() {
    ESP_LOGI(TAG, "Initializing CST9220 touch driver...");
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ_HZ);

    // Register LVGL input device
    lv_indev_drv_init(&_indevDrv);
    _indevDrv.type = LV_INDEV_TYPE_POINTER;
    _indevDrv.read_cb = touchReadCallback;
    _indevDrv.user_data = this;
    _indev = lv_indev_drv_register(&_indevDrv);

    ESP_LOGI(TAG, "CST9220 touch driver registered with LVGL.");
    return true;
}

void TouchCST9220::setRotation(uint8_t rotation) {
    _rotation = rotation % 4;
}

bool TouchCST9220::readTouch(int16_t *x, int16_t *y) {
    Wire.beginTransmission(CST9220_I2C_ADDR);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) {
        return false;
    }

    if (Wire.requestFrom((uint8_t)CST9220_I2C_ADDR, (uint8_t)6) != 6) {
        return false;
    }

    uint8_t data[6];
    for (int i = 0; i < 6; i++) {
        data[i] = Wire.read();
    }

    uint8_t fingerNum = data[2] & 0x0F;
    if (fingerNum == 0) {
        return false;
    }

    int16_t rawX = ((data[3] & 0x0F) << 8) | data[4];
    int16_t rawY = ((data[5] & 0x0F) << 8) | data[6];

    // Map according to rotation
    int16_t mappedX = rawX;
    int16_t mappedY = rawY;

    switch (_rotation) {
        case 1: // 90 deg
            mappedX = rawY;
            mappedY = LCD_WIDTH - 1 - rawX;
            break;
        case 2: // 180 deg
            mappedX = LCD_WIDTH - 1 - rawX;
            mappedY = LCD_HEIGHT - 1 - rawY;
            break;
        case 3: // 270 deg
            mappedX = LCD_HEIGHT - 1 - rawY;
            mappedY = rawX;
            break;
        case 0: // 0 deg
        default:
            mappedX = rawX;
            mappedY = rawY;
            break;
    }

    // Bound check
    if (mappedX < 0) mappedX = 0;
    if (mappedX >= LCD_WIDTH) mappedX = LCD_WIDTH - 1;
    if (mappedY < 0) mappedY = 0;
    if (mappedY >= LCD_HEIGHT) mappedY = LCD_HEIGHT - 1;

    *x = mappedX;
    *y = mappedY;
    return true;
}

void TouchCST9220::touchReadCallback(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    TouchCST9220 *inst = (TouchCST9220 *)drv->user_data;
    if (!inst) return;

    int16_t touchX = 0;
    int16_t touchY = 0;

    if (inst->readTouch(&touchX, &touchY)) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = touchX;
        data->point.y = touchY;
        inst->_lastX = touchX;
        inst->_lastY = touchY;
        inst->_isPressed = true;
    } else {
        data->state = LV_INDEV_STATE_REL;
        data->point.x = inst->_lastX;
        data->point.y = inst->_lastY;
        inst->_isPressed = false;
    }
}
