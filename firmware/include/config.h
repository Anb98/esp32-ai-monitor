#pragma once

#include <stdint.h>

// ==========================================
// Waveshare ESP32-S3-Touch-AMOLED-2.16 Pinout
// ==========================================

// Display (CO5300 QSPI 480x480)
#define LCD_QSPI_CS       9
#define LCD_QSPI_SCK      10
#define LCD_QSPI_D0       11
#define LCD_QSPI_D1       12
#define LCD_QSPI_D2       13
#define LCD_QSPI_D3       14
#define LCD_RST_PIN       8
#define LCD_TE_PIN        18
#define LCD_WIDTH         480
#define LCD_HEIGHT        480

// Touch (CST9220 I2C) & Shared I2C Bus
#define I2C_SDA_PIN       15
#define I2C_SCL_PIN       16
#define I2C_PORT_NUM      0
#define I2C_FREQ_HZ       400000
#define TOUCH_INT_PIN     21
#define TOUCH_RST_PIN     -1
#define CST9220_I2C_ADDR  0x5A

// IMU (QMI8658 6-Axis Accelerometer/Gyroscope)
#define IMU_I2C_ADDR      0x6B
#define IMU_INT1_PIN      4
#define IMU_DEBOUNCE_MS   300

// Audio Codec (ES8311 I2S & I2C Control)
#define ES8311_I2C_ADDR   0x18
#define I2S_BCLK_PIN      40
#define I2S_WS_PIN        41
#define I2S_DOUT_PIN      42
#define I2S_MCLK_PIN      39
#define PA_ENABLE_PIN     46
#define I2S_SAMPLE_RATE   16000

// PMIC (AXP2101) & Power Management
#define AXP2101_I2C_ADDR  0x34
#define PMIC_IRQ_PIN      3
#define BOOT_BUTTON_PIN   0

// Network & Dashboard API Defaults
#ifndef WIFI_SSID
#define DEFAULT_WIFI_SSID "YourWiFiSSID"
#else
#define DEFAULT_WIFI_SSID WIFI_SSID
#endif

#ifndef WIFI_PASS
#define DEFAULT_WIFI_PASS "YourWiFiPassword"
#else
#define DEFAULT_WIFI_PASS WIFI_PASS
#endif

#ifndef DASHBOARD_URL
#define DASHBOARD_API_URL "http://192.168.1.100:8080/api/dashboard"
#else
#define DASHBOARD_API_URL DASHBOARD_URL
#endif

#define POLL_INTERVAL_MS  30000
