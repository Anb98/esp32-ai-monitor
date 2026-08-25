#include "pmic_axp2101.h"
#include "display_co5300.h"
#include <Wire.h>
#include <driver/gpio.h>
#include "esp_log.h"

static const char *TAG = "AXP2101";

PMICAXP2101 pmic;

#define AXP2101_STATUS1     0x00
#define AXP2101_POWER_ON_OFF_DCDC 0x80
#define AXP2101_POWER_ON_OFF_LDO  0x90
#define AXP2101_BLDO1_VOLTAGE     0x96

PMICAXP2101::PMICAXP2101() : _screenSuspended(false), _lastButtonPressTime(0) {}

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
    io_conf.pin_bit_mask = (1ULL << BOOT_BUTTON_PIN);
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

void PMICAXP2101::handleButtonPress() {
    // Check BOOT button (active low)
    if (gpio_get_level((gpio_num_t)BOOT_BUTTON_PIN) == 0) {
        uint32_t now = millis();
        if (now - _lastButtonPressTime > 400) { // 400ms debounce
            _lastButtonPressTime = now;
            _screenSuspended = !_screenSuspended;

            if (_screenSuspended) {
                ESP_LOGI(TAG, "Suspending screen...");
                display.sleep();
                enableAMOLED(false);
            } else {
                ESP_LOGI(TAG, "Waking screen...");
                enableAMOLED(true);
                vTaskDelay(pdMS_TO_TICKS(20));
                display.wake();
            }
        }
    }
}
