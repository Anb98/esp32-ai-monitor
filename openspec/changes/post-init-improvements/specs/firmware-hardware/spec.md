# Delta for firmware-hardware

## MODIFIED Requirements

### Requirement: CST9220 I2C Capacitive Touch Driver
The firmware MUST interface with the CST9220 capacitive touch IC over I2C and register as an LVGL pointer input device (`lv_indev_drv_t`). Every register byte used to compute touch coordinates MUST come from bytes actually requested/returned by the I2C transaction — no index beyond the requested byte count.

#### Scenario: Touch input coordinate mapping
- **Given** an active touch event on the screen
- **When** the CST9220 generates an I2C coordinate read event
- **Then** the driver MUST map coordinates accurately to the 480x480 screen space
- **And** pass coordinate and press state data into LVGL.

#### Scenario: In-bounds register read
- **Given** the driver requests N bytes from the CST9220 (7 bytes for a full touch report)
- **When** it computes `rawX`/`rawY` from the returned buffer
- **Then** every byte index used MUST be `< N` — no read of `buffer[N]` or beyond.
(Previously: 6 bytes were requested but index 6 was read for the Y-coordinate low byte — an out-of-bounds stack read.)

---

### Requirement: ES8311 I2S Audio Codec Status Alerts
The firmware MUST interface with the ES8311 audio codec via I2C control and I2S data to play acoustic notifications on provider status transitions (degradation/outage tone; recovery chime), and playback MUST NOT block the network/polling task. Tone generation MUST run asynchronously (own task/queue or a non-blocking I2S write); triggering an alert MUST return control to the caller immediately rather than waiting for playback to finish.

#### Scenario: Provider status drops to degraded
- **Given** Claude Code status was previously `"operational"`
- **When** a new dashboard payload indicates Claude status is `"degraded"`
- **Then** the audio driver MUST play the degradation warning chime
- **And** subsequent polls with the same `"degraded"` status SHALL NOT repeat the chime.

#### Scenario: Alert playback does not delay polling
- **Given** a ~600ms alert tone is triggered in the same cycle as a scheduled `/api/dashboard` poll
- **When** the alert fires
- **Then** the poll MUST still be issued on its normal ~30s schedule
- **And** the call that triggers the tone MUST NOT block waiting for playback to complete.
(Previously: `i2s_write(..., portMAX_DELAY)` ran synchronously inside the network task, delaying the next poll by ~550-600ms.)

## ADDED Requirements

### Requirement: Build-Time Wi-Fi Credential Injection
Wi-Fi SSID and password MUST be supplied at build time via PlatformIO build flags (e.g., `-D WIFI_SSID=... -D WIFI_PASSWORD=...`) sourced from a gitignored secrets file, and MUST NOT appear as literal values in any tracked source file (including `config.h`).

#### Scenario: Secrets file is gitignored
- **Given** a developer creates the Wi-Fi secrets file per the README
- **When** the repository's `.gitignore` is inspected
- **Then** that secrets file MUST be excluded from version control.

#### Scenario: No hardcoded credentials in tracked files
- **Given** the repository's tracked firmware source (`config.h` and other committed files)
- **When** searched for literal `WIFI_SSID`/`WIFI_PASSWORD` value assignments
- **Then** no real credential value MUST be present in any tracked file — only a build-flag/macro reference.
