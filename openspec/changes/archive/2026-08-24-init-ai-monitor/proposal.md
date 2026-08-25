# Change: init-ai-monitor

## Why
Developers using Claude Code and Google Antigravity need real-time, glanceable visibility into quota consumption (5h rolling & 7d limits), token validity, and service health without interrupting their terminal workflow.

## What
Bootstrap the AI Monitor system comprising a lightweight Go backend service (<10MB RAM) in Docker and an ESP32-S3 embedded firmware driving a 2.16" 480x480 AMOLED display.

## Capabilities

### New Capabilities
- `dashboard-api`: Lightweight Go HTTP server exposing `GET /api/dashboard` and `/healthz` endpoints over local Wi-Fi.
- `provider-metrics`: Token parsers (`~/.claude`, `~/.gemini`), quota calculation engines (Claude 5h/7d, Antigravity), and upstream status checkers (status.claude.com component `yyzkbfz2thpt`, Google AI Studio).
- `firmware-ui`: LVGL 8/9 dual-view interface (Claude Code / Antigravity) with full-screen horizontal swipe transitions, page indicators (● ○ / ○ ●), quota progress bars, reset countdowns, and re-login alerts.
- `firmware-hardware`: Peripheral drivers for CO5300 QSPI AMOLED (480x480), CST9220 I2C Touch, QMI8658 6-axis IMU (4-way auto-rotation with 300ms debounce), ES8311 I2S Audio Codec (alert/recovery chimes), and AXP2101 PMIC (screen suspend/wake on BOOT/GPIO0).

## Rollback Plan
1. Delete generated backend source code, tests, and Dockerfile.
2. Delete firmware project files, drivers, and UI assets.
3. Remove `openspec/changes/init-ai-monitor` directory.
4. Stop and prune the Docker container if running.

## Success Criteria
- [ ] Go backend runs in Docker with memory footprint < 10MB RAM.
- [ ] Backend parses local credentials and accurately detects 401/403/expired token states.
- [ ] `GET /api/dashboard` returns structured JSON with quota, reset times, and service health.
- [ ] ESP32-S3 firmware connects to local Wi-Fi and fetches dashboard data periodically.
- [ ] LVGL UI renders crisp 480x480 views with smooth swipe gestures and page indicators.
- [ ] Display suspends/wakes on BOOT/GPIO0 press without disconnecting Wi-Fi.
- [ ] Screen auto-rotates across 4 orientations via QMI8658 with 300ms debounce.
- [ ] ES8311 audio plays distinct alert tones on quota warnings and recovery chimes.
- [ ] Strict TDD followed with Go unit test coverage >= 80%.
