# Firmware: Waveshare ESP32-S3-Touch-AMOLED-2.16

Firmware for the 2.16" 480x480 AMOLED Ambient AI Quota & Health Monitor.

## Hardware Specifications
- **MCU**: ESP32-S3R8 (Xtensa dual-core 32-bit LX7 @ 240MHz)
- **Memory**: 16MB Flash (Quad SPI), 8MB PSRAM (Octal SPI / OPI)
- **Display**: 2.16" 480x480 CO5300 QSPI AMOLED Panel
- **Touch**: CST9220 Capacitive Multi-Touch Controller (I2C)
- **IMU**: QMI8658 6-Axis Accelerometer & Gyroscope (I2C)
- **Audio**: ES8311 I2S Audio Codec + Onboard Power Amplifier (GPIO46)
- **PMIC**: AXP2101 Power Management IC (I2C)

## Pinout Map

| Function | Pin / GPIO | Description |
| :--- | :--- | :--- |
| **LCD_QSPI_CS** | `GPIO9` | Display Chip Select |
| **LCD_QSPI_SCK** | `GPIO10` | Display QSPI Clock |
| **LCD_QSPI_D0-D3** | `GPIO11-14` | Display Quad Data Lines |
| **LCD_RST** | `GPIO8` | Display Reset Pin |
| **LCD_TE** | `GPIO18` | Display Tearing Effect Pin |
| **I2C_SDA / SCL** | `GPIO15 / 16` | Shared I2C Bus (Touch, IMU, Audio, PMIC) |
| **TOUCH_INT** | `GPIO21` | Touch Interrupt Line |
| **IMU_INT1** | `GPIO4` | QMI8658 Interrupt Line |
| **I2S_BCLK** | `GPIO40` | Audio Bit Clock |
| **I2S_WS** | `GPIO41` | Audio Word Select / LRCK |
| **I2S_DOUT** | `GPIO42` | Audio Data Output |
| **I2S_MCLK** | `GPIO39` | Audio Master Clock |
| **PA_ENABLE** | `GPIO46` | Power Amplifier Enable |
| **AXP2101_IRQ** | `GPIO3` | PMIC Interrupt |
| **BOOT / SUSPEND**| `GPIO0` | Screen Suspend / Wake Toggle |

## Building & Flashing via PlatformIO

1. Set your Wi-Fi credentials and Backend URL in `include/config.h` (or pass via `-D` flags).
2. Connect the board over USB (e.g. `COM7` on Windows).
3. Build and upload using PlatformIO CLI:
```bash
pio run -e esp32-s3-touch-amoled-216 --target upload --upload-port COM7
pio device monitor -p COM7 -b 115200
```
