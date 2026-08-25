#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "config.h"

class DisplayCO5300 {
public:
    DisplayCO5300();
    bool init();
    void sleep();
    void wake();
    void setBrightness(uint8_t brightness);
    bool isSleeping() const { return _sleeping; }

    static void flushCallback(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p);

private:
    void initBus();
    void initPanel();
    void writeCmd(uint32_t cmd);
    void writeData(const uint8_t *data, size_t len);
    void writeCmdData(uint32_t cmd, const uint8_t *data, size_t len);

    bool _sleeping;
    lv_disp_draw_buf_t _drawBuf;
    lv_color_t *_buf1;
    lv_color_t *_buf2;
    lv_disp_drv_t _dispDrv;
};

extern DisplayCO5300 display;
