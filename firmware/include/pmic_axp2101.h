#pragma once

#include <Arduino.h>
#include "config.h"

class PMICAXP2101 {
public:
    PMICAXP2101();
    bool init();
    void setAMOLEDVoltage(uint16_t millivolts);
    void enableAMOLED(bool enable);
    void handleButtonPress();
    bool isScreenSuspended() const { return _screenSuspended; }

private:
    void writeRegister(uint8_t reg, uint8_t val);
    uint8_t readRegister(uint8_t reg);

    bool _screenSuspended;
    uint32_t _lastButtonPressTime;
};

extern PMICAXP2101 pmic;
