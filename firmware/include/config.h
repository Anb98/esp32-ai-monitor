#pragma once

#include <stdint.h>

// ==========================================
// Waveshare ESP32-S3-Touch-AMOLED-2.16 Pinout
// ==========================================

// Display (CO5300 QSPI 480x480)
#define LCD_QSPI_CS       12
#define LCD_QSPI_SCK      38
#define LCD_QSPI_D0       4
#define LCD_QSPI_D1       5
#define LCD_QSPI_D2       6
#define LCD_QSPI_D3       7
#define LCD_RST_PIN       39
#define LCD_TE_PIN        -1   // no TE line is broken out; GPIO18 is the user key
#define LCD_WIDTH         480
#define LCD_HEIGHT        480

// GPIO map verified against the official pinout at
// https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.16
// Touch (CST9220 I2C) & Shared I2C Bus
#define I2C_SDA_PIN       15
#define I2C_SCL_PIN       14
#define I2C_PORT_NUM      0
#define I2C_FREQ_HZ       400000
#define TOUCH_INT_PIN     11
#define TOUCH_RST_PIN     40
#define CST9220_I2C_ADDR  0x5A

// IMU (QMI8658 6-Axis Accelerometer/Gyroscope)
#define IMU_I2C_ADDR      0x6B
#define IMU_INT1_PIN      17
#define IMU_DEBOUNCE_MS   300

// Audio Codec (ES8311 I2S & I2C Control)
#define ES8311_I2C_ADDR   0x18
#define I2S_BCLK_PIN      9
#define I2S_WS_PIN        45
#define I2S_DOUT_PIN      8    // ES8311 DSDIN: data out of the ESP32, into the codec
#define I2S_MCLK_PIN      42
#define PA_ENABLE_PIN     46
#define I2S_SAMPLE_RATE   16000

// PMIC (AXP2101) & Power Management
#define AXP2101_I2C_ADDR  0x34
#define PMIC_IRQ_PIN      -1   // not broken out in the published pinout; unused
#define BOOT_BUTTON_PIN   0
#define KEY_BUTTON_PIN    18   // user key; BOOT is reserved for flashing

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
#define WIFI_RECONNECT_INTERVAL_MS 8000   // ms between reconnect attempts; a full association needs several seconds

// UI Behavior Tuning
#define AUTO_DIM_MS       15000  // ms of touch inactivity before dimming the AMOLED
#define AUTO_SLEEP_MS     0      // 0 = disabled; deeper AXP2101 power-off after this much inactivity
#define DIM_LEVEL         40     // 0-255 brightness while dimmed
#define STALE_AFTER_MS    90000  // ms without a successful poll before "SIN CONEXION"; must exceed POLL_INTERVAL_MS or the badge flashes every cycle
#define BATTERY_REFRESH_S 15     // seconds between battery indicator refreshes (ticks of the 1Hz UI timer)
