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

Verified against the vendor pinout at
<https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-2.16> and against
`examples/arduino/libraries/Mylibrary/pin_config.h` in
[waveshareteam/ESP32-S3-Touch-AMOLED-2.16](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.16).
**Do not "correct" these from memory or from another board in the family** —
the values here were wrong on first bring-up and cost a full debugging session.

| Function | Pin / GPIO | Description |
| :--- | :--- | :--- |
| **LCD_QSPI_CS** | `GPIO12` | Display Chip Select |
| **LCD_QSPI_SCK** | `GPIO38` | Display QSPI Clock |
| **LCD_QSPI_D0-D3** | `GPIO4-7` | Display Quad Data Lines |
| **LCD_RST** | `GPIO39` | Display Reset Pin |
| **LCD_TE** | *not exposed* | No TE line is broken out; `GPIO18` is the user key |
| **I2C_SDA / SCL** | `GPIO15 / 14` | Shared I2C Bus (Touch, IMU, PMIC) |
| **TOUCH_INT** | `GPIO11` | Touch Interrupt Line |
| **TOUCH_RST** | `GPIO40` | Touch Reset Line |
| **IMU_INT1** | `GPIO17` | QMI8658 Interrupt Line |
| **I2S_BCLK** | `GPIO9` | Audio Bit Clock |
| **I2S_WS** | `GPIO45` | Audio Word Select / LRCK |
| **I2S_DOUT** | `GPIO8` | ES8311 DSDIN: out of the ESP32, into the codec |
| **I2S_MCLK** | `GPIO42` | Audio Master Clock |
| **PA_ENABLE** | `GPIO46` | Power Amplifier Enable |
| **BOOT** | `GPIO0` | Boot button |

Board buttons are **KEY** (`GPIO18`), **PWR** (wired to the PMU, not a GPIO)
and **BOOT** (`GPIO0`). There is no labelled RESET button.

## Hardware Bring-Up Notes

Everything below was found by flashing this firmware onto real hardware for the
first time. Each item is a real failure with a real fix; they are documented
because none of them announce themselves in a compiler warning, and several
report success while doing nothing at all.

### The display stays black

Four independent causes, in the order they were hit:

1. **`lv_init()` ran too late.** `display.init()` registers a display driver,
   which allocates from LVGL's heap. `lv_init()` used to live inside
   `UIManager::init()`, sixteen lines further down `setup()`, so the first
   `lv_mem_alloc()` dereferenced a null pool and panicked the core in a boot
   loop. `lv_init()` now runs in `setup()` before anything touches LVGL.

2. **The QSPI framing was wrong.** On the CO5300 the real opcode travels in the
   *address* field of the transaction, behind a `0x02` prefix, and **both must
   go out on all four lines**. `writeCmd()` was missing
   `SPI_TRANS_MULTILINE_ADDR`, so every command was clocked out on a single
   line and the controller ignored all of them — including `Display ON`. The
   ESP32 reported success throughout, because it transmitted fine; nothing was
   listening. Pixel payloads use a different envelope again: prefix `0x32`,
   fixed address `0x003C00`, quad mode (`writePixels()`).

3. **The init sequence was incomplete.** `0xFE` (command page select) and
   `0xC4` (SPI mode control) were missing. Without `0xFE` the controller
   silently drops later writes. The sequence now mirrors `Arduino_CO5300.cpp`
   from the vendor SDK, including its much longer reset timings.

4. **The pinout was fiction.** 15 of 18 GPIOs did not match the board. Writing
   to the wrong pins produces no error anywhere: commands go into the void and
   the driver still logs "initialized successfully". The tell was that two of
   three I2C chips did not answer — one dead chip is a chip, three is a bus.

Note that the panel does **not** need the AXP2101 configured to light up: the
vendor's own `01_HelloWorld` never touches the PMIC. If the screen is dark, the
power rails are not the first place to look.

### The display is corrupted or the text looks italic

- **Speckles and warped glyphs in the areas being redrawn**: the LVGL draw
  buffers were in PSRAM, which the SPI DMA reads through the cache, so
  transfers picked up stale bytes for lines the CPU had just repainted. They
  must be `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`.
- **Sheared, italic-looking text**: the CO5300 addresses its framebuffer in
  pixel *pairs*. A redraw area starting on an odd column shifts every following
  line, which is exactly what repainting single countdown digits triggers. The
  `rounder_cb` snaps each flush to an even span.
- **Wrong colours entirely**: `LV_COLOR_16_SWAP` must be `1`. It is defined in
  **two** places — `platformio.ini` and `lv_conf.h` — and the header wins over
  the compiler flag, so changing only one does nothing.

### Rotation

Rotation is done in hardware via MADCTL (`DisplayCO5300::setRotation`), not
LVGL's `sw_rotate`. The panel is square, so orientation is purely a scan-order
change: no resolution swap and no per-flush CPU cost. `sw_rotate` rotates every
buffer on the CPU and corrupts partial redraws when it lands mid-flush.

Two values were measured on hardware and cannot be derived from any datasheet:

- Held upright, the accelerometer reads **`ax = -1.0`**. That is the zero
  position in `IMUQMI8658::calculateOrientation`.
- The panel is mounted **half a turn** from the controller's default scan
  order, so upright is MADCTL **`0xC0`**, not `0x00`.

If rotation ever turns the wrong way, swap `0xA0` and `0x60` in that table. If
it is off by a half turn, shift the table by two.

### Wi-Fi never associates

`WiFi.reconnect()` tears down the association already in flight. `taskLoop()`
runs every 100 ms, so calling it unconditionally restarted the handshake before
it could ever finish. Retries are throttled by
`WIFI_RECONNECT_INTERVAL_MS`. Several `AUTH_EXPIRE` / `AUTH_FAIL` events during
association are normal here; the link comes up about 7 s after boot.

### Threading

LVGL is not thread-safe and three tasks reach it. Anything touching LVGL from
outside `lvglTask` must hold `ui.uiLock()` — including the IMU's rotation
update, which is easy to miss because it looks like a hardware call.

The draw buffer is capped by the SPI DMA transaction limit (32768 B on the
ESP32-S3), which is what `kDrawBufLines` and its `static_assert` protect. Raise
the line count and flushes fail with "txdata transfer > hardware max supported
len".

### Touch, auto-dim and screen suspension

The touch controller's register map in `readTouch()` is **not trustworthy** —
it was written from the same guesswork as the original pinout, and reports a
non-zero finger count while idle. Two things follow from that:

- **TP_INT decides whether a finger is down, the I2C read only says where.**
  They are kept separate in `touchReadCallback()`, so a real touch that fails
  to parse still counts as user activity.
- **TP_INT is pulsed, not held low.** Level polling at LVGL's ~5 ms cadence
  caught roughly 2 touches in 58 polls. It is latched by an ISR
  (`GPIO_INTR_NEGEDGE`) and consumed by `drainInterrupt()`, which runs from the
  sensors task so waking never depends on the LVGL task polling first.

If you ever need gestures, multi-touch or on-screen buttons, pull the real
register map from the vendor driver first, the same way the CO5300 sequence
was recovered.

All wake paths go through `PMICAXP2101::wakeScreen()`: it restores brightness,
lifts suspension **and resets the idle timer**. Skipping that last step is what
made the key press look dead — the screen woke and auto-dim re-dimmed it 20 ms
later. Anything proving the user is present should call it; the IMU does, on a
confirmed orientation change.

`enableAMOLED()` is deliberately not used in the suspend path. It flips bit 0
of register `0x90`, which is not the rail the datasheet assigns to the panel,
so cutting it risks powering down a neighbouring chip. The panel sleeps through
its own controller commands instead.

Timing knobs live in `config.h`. `STALE_AFTER_MS` **must stay well above**
`POLL_INTERVAL_MS`: when both were 30000 the "SIN CONEXION" badge flashed once
per poll cycle on a perfectly healthy link.

### Debugging tips

- To capture boot logs, use `pio run -t upload -t monitor`. Opening the monitor
  separately does not reset the board, so the port just sits silent — and there
  is no RESET button to press.
- Decode a panic backtrace with
  `xtensa-esp32s3-elf-addr2line -pfiaC -e .pio/build/<env>/firmware.elf <addresses>`.
- The vendor's `01_HelloWorld` example is the reference for "the hardware
  works". Its `Arduino_GFX` version needs Arduino-ESP32 core v3, which this
  project does not use, so it will not build here unmodified.

## Installing PlatformIO

You need the `pio` CLI on `PATH`. With Python already installed:

```bash
pip install --user platformio
```

Verify with `pio --version`. If the command is not found, add Python's user
script directory to `PATH` (`%APPDATA%\Python\Python311\Scripts` on Windows),
or invoke it as `python -m platformio`. The VSCode PlatformIO extension works
too and ships its own copy.

## Building & Flashing via PlatformIO

1. Copy `secrets.ini.example` to `secrets.ini` and put your Wi-Fi SSID/password there.
   `secrets.ini` is git-ignored, so credentials never reach a commit; `platformio.ini`
   pulls it in via `extra_configs`. Override the backend URL with `-D DASHBOARD_URL=\"...\"`
   in the same file.
2. Connect the board over USB (e.g. `COM7` on Windows).
3. Build and upload using PlatformIO CLI:
```bash
cd firmware
pio run -e esp32-s3-touch-amoled-216 -t upload --upload-port COM7
```

To watch the boot log, chain both targets. Opening the monitor as a separate
command does **not** reset the board, and there is no RESET button, so the port
just sits silent and you miss startup entirely:

```bash
pio run -e esp32-s3-touch-amoled-216 -t upload -t monitor --upload-port COM7
```

Find the port with `pio device list`. PlatformIO rebuilds only what changed,
including after edits to `secrets.ini` — no clean needed.

### Changing Wi-Fi, the backend URL, or any secret

Edit `firmware/secrets.ini` and re-flash with the command above. Keep the
escaped quotes (`\"`) and change only the text inside them; they are what make
the value reach the compiler as a C string literal.

`DASHBOARD_URL` must point at the machine running the backend, reachable from
the device's network — not `localhost`. If the host's LAN address changes (a
new DHCP lease after a router reboot, for instance), the device shows
`SIN CONEXION` with no further clue.

### Changing the backend

The firmware talks to a container. After editing Go code:

```bash
docker compose up -d --build
```

**The `--build` matters.** Without it Compose reuses the existing image and the
device keeps polling the old backend while everything looks fine locally.
