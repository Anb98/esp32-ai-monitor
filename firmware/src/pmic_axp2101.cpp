#include "pmic_axp2101.h"
#include "ui_manager.h"
#include "display_co5300.h"
#include "touch_cst9220.h"
#include <Wire.h>
#include <driver/gpio.h>
#include "esp_log.h"

static const char *TAG = "AXP2101";

PMICAXP2101 pmic;

#define AXP2101_STATUS1     0x00
#define AXP2101_POWER_ON_OFF_DCDC 0x80
#define AXP2101_POWER_ON_OFF_LDO  0x90
#define AXP2101_BLDO1_VOLTAGE     0x96

PMICAXP2101::PMICAXP2101() : _screenSuspended(false), _dimmed(false), _buttonWasDown(false), _suspendedAtMs(0), _lastButtonPressTime(0) {}

void PMICAXP2101::writeRegister(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(AXP2101_I2C_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

uint8_t PMICAXP2101::readRegister(uint8_t reg) {
    Wire.beginTransmission(AXP2101_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) {
        return 0;
    }
    Wire.requestFrom((uint8_t)AXP2101_I2C_ADDR, (uint8_t)1);
    return Wire.read();
}

bool PMICAXP2101::init() {
    ESP_LOGI(TAG, "Initializing AXP2101 PMIC...");
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ_HZ);

    // Check device presence
    Wire.beginTransmission(AXP2101_I2C_ADDR);
    if (Wire.endTransmission() != 0) {
        ESP_LOGW(TAG, "AXP2101 PMIC not found on I2C bus (may be directly powered)");
    } else {
        // Set BLDO1 to 3.3V for AMOLED power
        setAMOLEDVoltage(3300);
        enableAMOLED(true);
    }

    // Configure BOOT button (GPIO0) as input with pullup
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << BOOT_BUTTON_PIN) | (1ULL << KEY_BUTTON_PIN);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "AXP2101 & BOOT button initialized.");
    return true;
}

void PMICAXP2101::setAMOLEDVoltage(uint16_t millivolts) {
    // BLDO1 voltage step configuration
    uint8_t step = (millivolts - 500) / 100;
    writeRegister(AXP2101_BLDO1_VOLTAGE, step);
}

void PMICAXP2101::enableAMOLED(bool enable) {
    uint8_t current = readRegister(AXP2101_POWER_ON_OFF_LDO);
    if (enable) {
        current |= 0x01; // Enable BLDO1
    } else {
        current &= ~0x01; // Disable BLDO1
    }
    writeRegister(AXP2101_POWER_ON_OFF_LDO, current);
}

// Restores full brightness and lifts suspension. Called by anything that
// proves the user is present: a touch, or the IMU seeing the device turned.
void PMICAXP2101::wakeScreen() {
    touch.markActivity();
    if (!_dimmed && !_screenSuspended) return;

    if (ui.uiLock()) {
        if (_screenSuspended) {
            display.wake();
            _screenSuspended = false;
        }
        display.setBrightness(255);
        _dimmed = false;
        ui.uiUnlock();
    }
}

void PMICAXP2101::updateAutoDim() {
    touch.drainInterrupt();

    // A touch lifts suspension too, not just dimming: the screen being off is
    // the most likely reason someone taps it. It has to be a touch that landed
    // *after* the screen went off, so this compares against the suspension
    // instant instead of an idle window; an idle window still counts the
    // activity that came before the key press, which woke the screen straight
    // back up. Nothing else runs while suspended: the panel is asleep, so
    // dimming it is a pointless SPI write.
    if (_screenSuspended) {
        if ((int32_t)(touch.lastTouchMs() - _suspendedAtMs) > 0) {
            wakeScreen();
        }
        return;
    }

    uint32_t idle = millis() - touch.lastTouchMs();

    if (AUTO_DIM_MS > 0) {
        if (!_dimmed && idle >= AUTO_DIM_MS) {
            display.setBrightness(DIM_LEVEL);
            _dimmed = true;
        } else if (_dimmed && idle < AUTO_DIM_MS) {
            display.setBrightness(255);
            _dimmed = false;
        }
    }

    // Deeper power-off step ships disabled (AUTO_SLEEP_MS == 0). Only the
    // panel is put to sleep: the rail is left alone for the same reason as in
    // handleButtonPress(). The user key wakes it back up.
    if (AUTO_SLEEP_MS > 0 && idle >= AUTO_SLEEP_MS) {
        _screenSuspended = true;
        _suspendedAtMs = millis();
        if (ui.uiLock()) {
            display.sleep();
            ui.uiUnlock();
        }
    }
}

void PMICAXP2101::handleButtonPress() {
    // User key, active low. BOOT is deliberately not used: holding it across a
    // reset drops the board into the bootloader.
    bool down = gpio_get_level((gpio_num_t)KEY_BUTTON_PIN) == 0;
    if (!down) {
        _buttonWasDown = false;
        return;
    }

    // Edge, not level: this runs every 20ms, so a level test toggles again for
    // every 400ms the key stays held, and an ordinary press long enough to
    // suspend the screen un-suspends it on the way out.
    if (_buttonWasDown) return;
    _buttonWasDown = true;

    uint32_t now = millis();
    if (now - _lastButtonPressTime <= 400) return; // contact bounce
    _lastButtonPressTime = now;

    // Branch on the current state and let each side own the flag: wakeScreen()
    // starts with an `if (!_dimmed && !_screenSuspended) return;` early-out, so
    // flipping the flag before calling it makes it return without ever issuing
    // display.wake(), leaving the panel asleep for good.
    //
    // The panel sleeps through its own controller commands; the PMIC rail is
    // deliberately left alone. enableAMOLED() flips bit 0 of register 0x90,
    // which is not the rail the datasheet assigns to the panel, so cutting it
    // here risks powering down a neighbour. display.sleep()/wake() are SPI
    // writes issued from the sensors task, so they need the same lock the
    // LVGL task holds.
    if (_screenSuspended) {
        ESP_LOGI(TAG, "Waking screen...");
        wakeScreen();
    } else {
        ESP_LOGI(TAG, "Suspending screen...");
        // Consume any edge latched before this press so it is not read as a
        // post-suspend touch on the next pass.
        touch.drainInterrupt();
        _screenSuspended = true;
        _suspendedAtMs = millis();
        if (ui.uiLock()) {
            display.sleep();
            ui.uiUnlock();
        }
    }
}
