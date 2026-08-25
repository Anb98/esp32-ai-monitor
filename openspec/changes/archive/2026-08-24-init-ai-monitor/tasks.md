# Tasks: init-ai-monitor

## Review Workload Forecast
Decision needed before apply: No
Chained PRs recommended: No
Chain strategy: size-exception
400-line budget risk: Medium

---

## Phase 1: Go Backend Foundation & Data Models

- [x] 1.1 **Initialize Go Module & Directory Scaffold**
  - Create `backend/go.mod` specifying Go 1.22+ module `github.com/esp32-ai-monitor/backend`.
  - Create directory layout: `cmd/server/`, `internal/model/`, `internal/config/`, `internal/provider/`, `internal/cache/`, `internal/api/`.
  - Dependencies: standard library only (zero external framework overhead for memory budget).
- [x] 1.2 **Implement Data Models (`internal/model/dashboard.go`)**
  - Define `ProviderStatus` enum (`"operational"`, `"degraded"`, `"outage"`).
  - Define `QuotaWindow` struct (`Used`, `Limit`, `Percentage`, `ResetTime`, `ResetTimestamp`).
  - Define `ProviderMetrics` struct (`Quota5h`, `QuotaWeekly`).
  - Define `Provider` struct (`ID`, `Name`, `Status`, `AuthValid`, `ReLoginRequired`, `Metrics`).
  - Define `SystemMetrics` struct (`UptimeSeconds`, `MemoryMB`).
  - Define `DashboardResponse` struct (`Timestamp`, `Providers`, `System`).
  - Add JSON tags conforming strictly to the `GET /api/dashboard` schema.
- [x] 1.3 **Implement Backend Configuration Loader (`internal/config/config.go`)**
  - Implement `Config` struct with `Port`, `PollInterval`, `ClaudeConfigDir`, `GeminiConfigDir`.
  - Implement `LoadConfig()` with environment variable overrides: `PORT`, `CLAUDE_CONFIG_DIR`, `GEMINI_CONFIG_DIR`.
  - Fallback cleanly to `$HOME/.claude` and `$HOME/.gemini` when environment variables are unset.
- [x] 1.4 **Implement TDD Unit Tests for Configuration (`internal/config/config_test.go`)**
  - Test default fallback to user home directory.
  - Test environment variable override paths and port assignment.
  - Achieve 100% test coverage for config module.

---

## Phase 2: Go Backend Provider Engines & Watchers

- [x] 2.1 **Implement Claude Provider Engine (`internal/provider/claude.go`)**
  - Parse credentials from `~/.claude` (and `~/.claude.json` / session files).
  - Extract session token and validate structure.
  - Compute 5-hour rolling quota and 7-day weekly quota percentages and reset times (`HH:MM`, `Dom 00:00`).
  - Handle missing, empty, or malformed credentials by setting `AuthValid: false` and `ReLoginRequired: true`.
- [x] 2.2 **Implement Claude Provider Unit Tests (`internal/provider/claude_test.go`)**
  - Test valid credential parsing with accurate percentage and reset timestamp computation.
  - Test missing file, empty file, and invalid JSON scenarios.
  - Test simulated 401/403 expired token conditions triggering `ReLoginRequired: true`.
  - Achieve >= 85% test coverage.
- [x] 2.3 **Implement Google Antigravity Provider Engine (`internal/provider/antigravity.go`)**
  - Parse credentials from `~/.gemini/credentials.json` or session files.
  - Extract token and calculate 5-hour and weekly quota metrics.
  - Set `AuthValid` and `ReLoginRequired` flags based on token presence and API response validation.
- [x] 2.4 **Implement Antigravity Provider Unit Tests (`internal/provider/antigravity_test.go`)**
  - Test valid credential parsing and quota calculations.
  - Test corrupted/missing token handling.
  - Test auto-recovery state when valid credentials replace expired credentials.
  - Achieve >= 85% test coverage.
- [x] 2.5 **Implement Upstream Status Scraper (`internal/provider/status_scraper.go`)**
  - Implement HTTP client querying `https://status.claude.com/api/v2/summary.json`.
  - Filter `components` specifically for component ID `yyzkbfz2thpt` (Claude Code).
  - Map upstream statuses: `operational` -> `StatusOperational`, `degraded_performance`/`under_maintenance` -> `StatusDegraded`, `major_outage`/`partial_outage` -> `StatusOutage`.
  - Implement Google AI Studio / Cloud service health status checking.
  - Retain last known valid status upon network failure without crashing or throwing unhandled errors.
- [x] 2.6 **Implement Status Scraper Unit Tests (`internal/provider/status_scraper_test.go`)**
  - Mock upstream JSON payloads for Claude component `yyzkbfz2thpt`.
  - Test operational, degraded, and outage state transitions.
  - Test network timeout / upstream 500 error behavior ensuring last valid state is retained.
  - Achieve >= 85% test coverage.
- [x] 2.7 **Implement Token Watcher & Provider Orchestrator (`internal/provider/provider.go`, `internal/provider/token_watcher.go`)**
  - Define `ProviderEngine` interface (`FetchMetrics(ctx) (*model.Provider, error)`).
  - Implement periodic scanner / filesystem poller orchestrating Claude and Antigravity metrics collection.
  - Implement automatic recovery detection when user re-authenticates via CLI (`claude` or `agy login`).
- [x] 2.8 **Implement Token Watcher Unit Tests (`internal/provider/token_watcher_test.go`)**
  - Test provider orchestration loop and periodic updates.
  - Test auto-recovery lifecycle on mocked file changes.

---

## Phase 3: Go Backend In-Memory Cache & HTTP Handlers

- [x] 3.1 **Implement In-Memory Thread-Safe Cache (`internal/cache/cache.go`)**
  - Implement `Cache` struct wrapping `model.DashboardResponse` protected by `sync.RWMutex`.
  - Provide `Set(data *model.DashboardResponse)` with write-lock.
  - Provide `Get() *model.DashboardResponse` with read-lock (<1ms read response).
- [x] 3.2 **Implement Cache Unit Tests (`internal/cache/cache_test.go`)**
  - Test concurrent reads and writes with `-race` flag.
  - Verify data integrity under high concurrent read loads.
- [x] 3.3 **Implement HTTP Handlers & Middleware (`internal/api/handler.go`, `internal/api/middleware.go`, `internal/api/router.go`)**
  - Implement `GET /api/dashboard` handler returning JSON payload with `Content-Type: application/json`.
  - Implement `GET /healthz` handler returning `{"status":"ok"}`.
  - Implement logging, CORS headers, and panic recovery middleware.
  - Ensure zero external web framework dependencies (stdlib `net/http`).
- [x] 3.4 **Implement API Handler Unit Tests (`internal/api/handler_test.go`)**
  - Test `GET /api/dashboard` schema compliance and HTTP 200 response using `httptest.NewRecorder`.
  - Test `GET /healthz` response.
  - Test sub-10ms response latency under concurrent client requests.
  - Achieve >= 85% test coverage.
- [x] 3.5 **Implement Main Application Entrypoint (`cmd/server/main.go`)**
  - Wire configuration, providers, background poller (30s ticker), in-memory cache, and HTTP server.
  - Calculate `system.uptime_seconds` and `system.memory_mb` from runtime memory stats (`runtime.ReadMemStats`).
  - Implement graceful shutdown handling on `SIGINT` / `SIGTERM`.

---

## Phase 4: Go Backend Docker & Compose

- [x] 4.1 **Create Multi-Stage Dockerfile (`backend/Dockerfile`)**
  - Stage 1: `golang:1.22-alpine` builder creating static binary with `-ldflags="-s -w"`.
  - Stage 2: `alpine:3.20` runtime with `ca-certificates` and non-root user `appuser`.
  - Configure `ENV GOMEMLIMIT=8MiB` and expose port `8080`.
- [x] 4.2 **Create Docker Compose Configuration (`backend/docker-compose.yml`)**
  - Define `ai-monitor-backend` service with read-only volume mounts: `${HOME}/.claude:/root/.claude:ro` and `${HOME}/.gemini:/root/.gemini:ro`.
  - Configure health check (`wget -qO- http://localhost:8080/healthz`).
  - Configure port mapping `8080:8080`.
- [x] 4.3 **Validate Docker Build & Memory Footprint**
  - Build and run container locally.
  - Measure Resident Set Size (RSS) to verify <10MB RAM ceiling.

---

## Phase 5: ESP32 Hardware Drivers

- [x] 5.1 **Configure PlatformIO Environment (`firmware/platformio.ini`)**
  - Target: `esp32-s3-devkitc-1` / ESP32-S3R8 (16MB Flash, 8MB PSRAM OPI).
  - Framework: `arduino` / ESP-IDF.
  - Dependencies: LVGL (v8.3.x or v9.x), ArduinoJson, I2C / QSPI support libraries.
  - Build flags: PSRAM enabled, LVGL configuration flags, display dimensions 480x480.
- [x] 5.2 **Create Hardware Pinout Definitions (`firmware/include/config.h`)**
  - Define CO5300 QSPI pins: `LCD_QSPI_CS` (9), `LCD_QSPI_SCK` (10), `LCD_QSPI_D0-D3` (11-14), `LCD_RST_PIN` (8), `LCD_TE_PIN` (18).
  - Define CST9220 Touch & I2C bus pins: `I2C_SDA_PIN` (15), `I2C_SCL_PIN` (16), `TOUCH_INT_PIN` (21).
  - Define QMI8658 IMU: `IMU_I2C_ADDR` (0x6B), `IMU_INT1_PIN` (4).
  - Define ES8311 Audio: `ES8311_I2C_ADDR` (0x18), `I2S_BCLK_PIN` (40), `I2S_WS_PIN` (41), `I2S_DOUT_PIN` (42), `I2S_MCLK_PIN` (39), `PA_ENABLE_PIN` (46).
  - Define AXP2101 PMIC: `AXP2101_I2C_ADDR` (0x34), `PMIC_IRQ_PIN` (3), `BOOT_BUTTON_PIN` (0).
  - Define Wi-Fi and API endpoint defaults (`DASHBOARD_API_URL`, `POLL_INTERVAL_MS`).
- [x] 5.3 **Implement CO5300 QSPI AMOLED Display Driver (`firmware/include/display_co5300.h`, `firmware/src/display_co5300.cpp`)**
  - Implement QSPI initialization sequence for CO5300 controller at 480x480 resolution (RGB565).
  - Implement LVGL display driver callback (`disp_flush`).
  - Allocate double draw buffers in PSRAM for smooth 60fps tearing-free rendering.
  - Implement display sleep commands (`0x10 Sleep In` / `0x11 Sleep Out`).
- [x] 5.4 **Implement CST9220 I2C Capacitive Touch Driver (`firmware/include/touch_cst9220.h`, `firmware/src/touch_cst9220.cpp`)**
  - Implement I2C communication with CST9220 touch IC.
  - Map touch points to 480x480 screen coordinate space.
  - Implement LVGL pointer input driver callback (`touch_read`).
- [x] 5.5 **Implement QMI8658 6-Axis IMU Auto-Rotation Driver (`firmware/include/imu_qmi8658.h`, `firmware/src/imu_qmi8658.cpp`)**
  - Read accelerometer XYZ gravity vectors over I2C.
  - Determine quadrant orientation: 0° (Normal), 90° (Right), 180° (Inverted), 270° (Left).
  - Implement 300ms software debounce filter to reject transient vibrations or tilts.
  - Update `lv_disp_set_rotation()` and touch coordinate transformation on validated orientation change.
- [x] 5.6 **Implement ES8311 I2S Audio Codec Driver & Synthesizer (`firmware/include/audio_es8311.h`, `firmware/src/audio_es8311.cpp`)**
  - Initialize ES8311 over I2C and configure I2S DAC output clocking.
  - Implement non-blocking audio tone generator using DMA.
  - Synthesize degradation warning tone (440Hz -> 330Hz descending alert).
  - Synthesize recovery chime (pleasant ascending triad: C5 -> E5 -> G5).
- [x] 5.7 **Implement AXP2101 PMIC & Screen Suspend/Wake Handler (`firmware/include/pmic_axp2101.h`, `firmware/src/pmic_axp2101.cpp`)**
  - Configure AXP2101 power rails (enable BLDO for AMOLED power).
  - Attach interrupt / polling for BOOT/GPIO0 button presses.
  - On button press: toggle CO5300 sleep mode and AMOLED power rail while keeping ESP32 CPU, Wi-Fi, and background tasks active.

---

## Phase 6: ESP32 LVGL UI Engine & Gestures

- [x] 6.1 **Implement LVGL UI Manager Core (`firmware/include/ui_manager.h`, `firmware/src/ui_manager.cpp`)**
  - Initialize LVGL framework, styles, dark-mode color palette, and custom fonts.
  - Create dual-screen container (Screen 1: Claude Code, Screen 2: Antigravity).
- [x] 6.2 **Implement Claude Code & Antigravity Screen Layouts**
  - Implement provider header (Name + Logo).
  - Implement status pill (🟢 Operational / 🟡 Degraded / 🔴 Outage) with dynamic background/text colors.
  - Implement 5-hour quota horizontal progress bar with percentage label and reset time (e.g. `15:30`).
  - Implement weekly quota horizontal progress bar with percentage label and reset time (e.g. `Dom 00:00`).
  - Configure dynamic bar color thresholding: Green (<70%), Yellow (70-90%), Red (>90%).
- [x] 6.3 **Implement Gesture Navigation & Page Indicator Dots**
  - Attach touch gesture event listener for horizontal swipes (left/right).
  - Transition between Screen 1 and Screen 2 with animated slide effect.
  - Render subtle bottom page indicator dots (`● ○` on Screen 1, `○ ●` on Screen 2).
  - Ensure zero on-screen navigation buttons for maximum screen real estate.
- [x] 6.4 **Implement Re-Login Required Warning Overlay Card**
  - Create modal overlay card with 🔑 icon, `"RE-LOGIN REQUERIDO"` title, and instruction text `"Ejecuta 'claude' o 'agy login' en terminal"`.
  - Display card when `re_login_required == true` or `auth_valid == false`.
  - Automatically dismiss card and restore normal quota bars when valid credentials return.

---

## Phase 7: ESP32 Network Client & Main Integration Loop

- [x] 7.1 **Implement Network Client FreeRTOS Task (`firmware/include/network_client.h`, `firmware/src/network_client.cpp`)**
  - Manage Wi-Fi association with auto-reconnect logic.
  - Periodically issue HTTP `GET /api/dashboard` requests (every 30 seconds).
  - Parse JSON response using ArduinoJson into local telemetry structs.
  - Update UI state via thread-safe queue / LVGL task handler.
- [x] 7.2 **Implement Status Transition Event Detector & Audio Dispatcher**
  - Compare new provider statuses against previous polling cycle.
  - If status drops to `"degraded"` or `"outage"` -> trigger ES8311 degradation alert tone.
  - If status recovers to `"operational"` -> trigger ES8311 recovery chime.
- [x] 7.3 **Implement Main Firmware Integration (`firmware/src/main.cpp`)**
  - Orchestrate hardware init sequence: PMIC -> Display -> Touch -> IMU -> Audio -> Wi-Fi.
  - Start FreeRTOS tasks: LVGL timer handler, IMU auto-rotation task (debounced), Network polling task, Button monitor task.
  - Verify stable execution and memory allocation.

---

## Phase 8: Verification & End-to-End Build Validation

- [x] 8.1 **Execute Backend TDD Test Suite & Code Coverage Check**
  - Run `go test -v -race -cover ./...` inside `backend/`.
  - Verify all unit tests pass with total statement coverage >= 80%.
- [x] 8.2 **Validate Backend Docker Build & Memory Consumption**
  - Run `docker compose build` and start container.
  - Verify `GET /healthz` returns `{"status":"ok"}`.
  - Verify `GET /api/dashboard` returns valid metrics.
  - Measure process RSS to verify <10MB RAM ceiling.
- [x] 8.3 **Compile PlatformIO Firmware Target**
  - Run `pio run` inside `firmware/` directory to verify clean compilation without compiler warnings or linker errors.
- [x] 8.4 **End-to-End System Verification**
  - Validate contract alignment between Go backend JSON output and ESP32 ArduinoJson parser.
  - Validate gesture transitions, IMU auto-rotation debounce, audio alert dispatching, and screen suspend/wake logic.
