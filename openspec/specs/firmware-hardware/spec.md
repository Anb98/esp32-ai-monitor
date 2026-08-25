# Capability: firmware-hardware

## Overview
The `firmware-hardware` capability encompasses the embedded driver integration and peripheral abstraction for the Waveshare ESP32-S3-Touch-AMOLED-2.16 board. It manages the CO5300 QSPI AMOLED display controller, CST9220 I2C capacitive touch panel, QMI8658 6-axis IMU auto-rotation, ES8311 I2S audio codec alerts, and AXP2101 PMIC power management with non-destructive screen suspend/wake toggling.

---

## Requirements

### REQ-HW-001: CO5300 QSPI AMOLED Display Driver
The firmware MUST initialize the CO5300 display controller over QSPI at 480x480 resolution (RGB565 format) and integrate with LVGL draw buffers.

#### Scenario: Display initialization and buffer allocation
- **Given** board startup sequence
- **When** the display driver initializes
- **Then** the QSPI bus MUST be configured with Quad SPI transactions
- **And** LVGL display buffers MUST be allocated in PSRAM/SRAM
- **And** the display panel MUST initialize without visual artifacts or tearing.

---

### REQ-HW-002: CST9220 I2C Capacitive Touch Driver
The firmware MUST interface with the CST9220 capacitive touch IC over I2C and register as an LVGL pointer input device (`lv_indev_drv_t`).

#### Scenario: Touch input coordinate mapping
- **Given** an active touch event on the screen
- **When** the CST9220 generates an I2C coordinate read event
- **Then** the driver MUST map coordinates accurately to the 480x480 screen space
- **And** pass coordinate and press state data into LVGL.

---

### REQ-HW-003: QMI8658 6-Axis IMU Auto-Rotation with Debounce
The firmware MUST poll the QMI8658 accelerometer/gyroscope, compute gravity vector orientation across 4 cardinal orientations (0°, 90°, 180°, 270°), and apply a 300ms debounce filter before changing the LVGL display rotation and touch mapping.

#### Scenario: Physical device rotation
- **Given** the device is resting at 0° (normal portrait)
- **When** the user rotates the device 90° clockwise and holds it steady for at least 300 milliseconds
- **Then** the IMU driver MUST detect the orientation change
- **And** trigger `lv_disp_set_rotation(disp, LV_DISP_ROT_90)`
- **And** update the touch driver coordinate transform to match the new orientation.

#### Scenario: Transient shake or brief vibration rejection
- **Given** the device is at 0°
- **When** a brief shock or tilt occurs lasting less than 300 milliseconds before returning to 0°
- **Then** the IMU driver MUST NOT trigger a display rotation.

---

### REQ-HW-004: ES8311 I2S Audio Codec Status Alerts
The firmware MUST interface with the ES8311 audio codec via I2C control and I2S data to play acoustic notifications on provider status transitions:
1. **Degradation/Outage Warning Tone**: Played when any provider transitions from `"operational"` to `"degraded"` or `"outage"`.
2. **Operational Recovery Chime**: Played when a provider transitions from `"degraded"` or `"outage"` back to `"operational"`.

#### Scenario: Provider status drops to degraded
- **Given** Claude Code status was previously `"operational"`
- **When** a new dashboard payload indicates Claude status is `"degraded"`
- **Then** the audio driver MUST play the degradation warning chime
- **And** subsequent dashboard polls with the same `"degraded"` status SHALL NOT repeat the chime.

#### Scenario: Provider status recovers to operational
- **Given** Claude Code was previously in `"degraded"` or `"outage"` state
- **When** the dashboard payload reports Claude status is `"operational"`
- **Then** the audio driver MUST play the ascending recovery chime.

---

### REQ-HW-005: AXP2101 PMIC and Screen Suspend/Wake Toggle
The firmware MUST configure the AXP2101 PMIC power channels and handle physical BOOT/GPIO0 button presses (or PMIC PWR key events) to toggle the AMOLED display between active and suspended/sleep states without terminating Wi-Fi or background telemetry polling.

#### Scenario: Short press suspends AMOLED display
- **Given** the display is currently illuminated and active
- **When** the user short-presses the BOOT button (or power button)
- **Then** the firmware MUST send display sleep commands to the CO5300 (or disable AMOLED power rail)
- **And** the Wi-Fi connection and background HTTP polling task MUST remain fully active.

#### Scenario: Short press wakes AMOLED display
- **Given** the display is in suspended/sleep state
- **When** the user short-presses the button again
- **Then** the firmware MUST re-enable the display and refresh the LVGL buffer with the latest cached telemetry.
