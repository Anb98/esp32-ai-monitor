#pragma once

#include <Arduino.h>
#include "config.h"

class PMICAXP2101 {
public:
    struct BatteryStatus {
        bool valid;      // false = I2C read failed or PMIC absent; other fields undefined
        bool present;
        bool charging;
        uint8_t percent; // 0-100
    };

    PMICAXP2101();
    bool init();
    void setAMOLEDVoltage(uint16_t millivolts);
    void enableAMOLED(bool enable);
    void handleButtonPress();
    void updateAutoDim();
    void wakeScreen();
    bool isScreenSuspended() const { return _screenSuspended; }
    BatteryStatus batteryStatus();

private:
    void writeRegister(uint8_t reg, uint8_t val);
    uint8_t readRegister(uint8_t reg);
    bool readRegister(uint8_t reg, uint8_t &out);

    bool _found;
    bool _screenSuspended;
    bool _dimmed;
    bool _buttonWasDown;
    uint32_t _suspendedAtMs;
    uint32_t _lastButtonPressTime;
};

extern PMICAXP2101 pmic;
