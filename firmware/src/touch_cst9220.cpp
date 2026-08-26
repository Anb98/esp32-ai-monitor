#include "touch_cst9220.h"
#include <driver/gpio.h>
#include <Wire.h>
#include "esp_log.h"

static const char *TAG = "CST9220";

TouchCST9220 touch;

TouchCST9220::TouchCST9220() : _rotation(0), _lastX(0), _lastY(0), _isPressed(false), _lastTouchMs(0), _indev(nullptr) {}

bool TouchCST9220::init() {
    ESP_LOGI(TAG, "Initializing CST9220 touch driver...");
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ_HZ);
    _lastTouchMs = millis();

    // The controller pulls TP_INT low only while a finger is actually down.
    // It is the authority on "is something touching", which matters because
    // the register layout below reports a non-zero finger count even when
    // idle: without this gate every poll looked like a touch and the
    // inactivity timer that drives auto-dim never advanced past a few ms.
    gpio_config_t int_conf = {};
    int_conf.pin_bit_mask = (1ULL << TOUCH_INT_PIN);
    int_conf.mode = GPIO_MODE_INPUT;
    int_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    int_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    int_conf.intr_type = GPIO_INTR_NEGEDGE;
    gpio_config(&int_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add((gpio_num_t)TOUCH_INT_PIN, touchIsr, nullptr);

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

// TP_INT is pulsed, not held: by the time a poll runs the line is usually back
// high, which is why level polling caught only a couple of touches out of many.
// The ISR latches the edge and drainInterrupt() consumes it.
static volatile bool s_touchEdge = false;

void IRAM_ATTR TouchCST9220::touchIsr(void *arg) {
    s_touchEdge = true;
}

void TouchCST9220::drainInterrupt() {
    if (s_touchEdge) {
        s_touchEdge = false;
        markActivity();
    }
}

bool TouchCST9220::readTouch(int16_t *x, int16_t *y) {
    // No interrupt asserted means no finger; skip the I2C round trip entirely.
    if (gpio_get_level((gpio_num_t)TOUCH_INT_PIN) != 0) {
        return false;
    }

    Wire.beginTransmission(CST9220_I2C_ADDR);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) {
        return false;
    }

    if (Wire.requestFrom((uint8_t)CST9220_I2C_ADDR, (uint8_t)7) != 7) {
        return false;
    }

    uint8_t data[7];
    for (int i = 0; i < 7; i++) {
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

    // TP_INT is the authority on whether a finger is down; the I2C read only
    // supplies coordinates. Keeping these separate matters because the
    // register layout is unreliable: if a real touch fails to parse we still
    // count it as activity, so auto-dim wakes on any touch rather than only on
    // ones we could decode.
    bool edge = s_touchEdge;
    inst->drainInterrupt();
    bool active = edge || gpio_get_level((gpio_num_t)TOUCH_INT_PIN) == 0;

    int16_t touchX = 0;
    int16_t touchY = 0;
    if (active && inst->readTouch(&touchX, &touchY)) {
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
