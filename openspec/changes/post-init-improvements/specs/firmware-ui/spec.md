# Delta for firmware-ui

## MODIFIED Requirements

### Requirement: Visual Status Pill and Quota Elements
Each provider screen MUST render the following visual elements in a cohesive 480x480 dark-mode theme:
1. **Header**: Provider name and logo icon.
2. **Status Pill**:
   - 🟢 `"Operativo"` / `"Operational"` with green accent when `status == "operational"`.
   - 🟡 `"Degradado"` / `"Degraded"` with yellow/amber accent when `status == "degraded"`.
   - 🔴 `"Interrupción"` / `"Outage"` with red accent when `status == "outage"`.
3. **5-Hour Quota Progress Bar**: horizontal bar 0-100%, percentage + reset-time label, dynamic color by threshold (Green <70%, Yellow 70-90%, Red >90%).
4. **Weekly Quota Progress Bar**: horizontal bar 0-100%, percentage + reset day/time label.
5. **Unavailable Quota State**: when `quota_5h.available` (or `quota_weekly.available`) is `false`, the corresponding bar MUST render dimmed/grey (not green/yellow/red) and its label MUST read the literal text `"Sin datos"` instead of any percentage or reset time.
(Previously: bars always rendered a percentage computed from `used/limit`; no distinction existed between real 0% usage and no data.)

#### Scenario: Quota bar color shifts above threshold
- **Given** the 5-hour quota usage reaches 92.0%
- **When** the dashboard state updates from the backend payload
- **Then** the progress bar fill color MUST transition to red
- **And** the text label MUST display `"92.0%"` alongside the updated reset time.

#### Scenario: Unavailable quota renders dimmed with "Sin datos"
- **Given** `quota_5h.available == false` for the active provider
- **When** the screen renders
- **Then** the bar fill MUST be dimmed/grey (never green/yellow/red)
- **And** the label MUST read exactly `"Sin datos"` (never a percentage, never blank, never `"0%"`).

### Requirement: Re-Login Required Warning Overlay
The UI MUST display a prominent warning overlay card over the quota metrics area ONLY when `re_login_required == true` (equivalently `auth_state == "expired"`) for an active provider:
- Icon: 🔑
- Title: `"RE-LOGIN REQUERIDO"`
- Instructions: `"Ejecuta 'claude' o 'agy login' en terminal"`

`auth_valid == false` alone MUST NOT trigger this overlay. In particular, `auth_state == "unknown"` (a transient auth-probe failure, distinct from a confirmed expiry) MUST NOT show the re-login overlay and MUST instead route to the stale/disconnected presentation.
(Previously: "When `re_login_required` is `true` or `auth_valid` is `false` for an active provider, the UI MUST display a prominent warning overlay card over the quota metrics area: Icon: 🔑; Title: `"RE-LOGIN REQUERIDO"`; Instructions: `"Ejecuta 'claude' o 'agy login' en terminal"`." This conflated a confirmed expired session with a merely-unconfirmed one, which would false-positive the re-login prompt on transient network failures.)

#### Scenario: Display warning card on auth expiration
- **Given** the ESP32 receives a dashboard payload with `claude.re_login_required == true`
- **When** Screen 1 (Claude Code) renders
- **Then** the UI MUST display the re-login warning card prominently in the center of Screen 1.

#### Scenario: Automatic dismissal of warning card
- **Given** the warning card is currently visible on Screen 1
- **When** the next dashboard payload reports `claude.re_login_required == false` and `claude.auth_valid == true`
- **Then** the warning card MUST be smoothly hidden and the regular quota bars MUST be displayed.

#### Scenario: Unknown auth state does not show the re-login overlay
- **Given** the ESP32 receives a dashboard payload with `auth_state == "unknown"`, `auth_valid == false`, and `re_login_required == false` for the active provider
- **When** the screen renders
- **Then** the re-login warning overlay MUST NOT be displayed
- **And** the screen MAY instead show the stale/disconnected indicator.

## ADDED Requirements

### Requirement: Serialized LVGL Access
The firmware MUST serialize every LVGL API call — `lv_timer_handler()` and any UI-update call originating outside the LVGL task (e.g., network-task-triggered `updateClaude`/`updateAntigravity`) — behind one shared mutex. No task other than the owning LVGL task MUST call any `lv_*` function without holding that lock, and the lock MUST NOT be held across blocking I/O.

#### Scenario: Cross-task update acquires the shared lock
- **Given** the network task receives a new dashboard payload on a different core than the LVGL task
- **When** it calls into the UI layer to update bars/labels
- **Then** it MUST acquire the shared LVGL mutex before any `lv_*` call and release it immediately after
- **And** the LVGL task's `lv_timer_handler()` call MUST also acquire the same mutex for its duration.

#### Scenario: Lock not held across blocking I/O
- **Given** the LVGL mutex was held for a UI update
- **When** that same task next performs a blocking network read
- **Then** the mutex MUST already be released before the blocking call begins.

### Requirement: Stale/Disconnected Indicator
The firmware MUST track the age of the last successfully received dashboard payload and display an explicit stale/offline indicator once that age exceeds approximately 30 seconds, independent of the previously-shown values.

#### Scenario: Payload loss triggers the offline indicator
- **Given** the last successful `/api/dashboard` poll was more than 30 seconds ago
- **When** the UI refreshes
- **Then** it MUST display an explicit stale/offline indicator distinct from the normal connected state.

#### Scenario: Fresh payload clears the indicator
- **Given** the indicator is currently showing stale/offline
- **When** a new dashboard payload is received
- **Then** the indicator MUST be cleared/hidden immediately.

### Requirement: Large Live Reset Countdown (Lower-Half Exclusive)
Each provider screen MUST render one large, continuously-updating countdown to the next `quota_5h` reset, seeded from the backend-computed `quota_5h.reset_in_seconds` on each poll and ticked locally between polls (millis-based; the firmware has no NTP/wall clock and does not derive the countdown from `reset_timestamp`), as the SOLE element occupying the bottom half of the 480x480 screen; no other widget, label, or the weekly quota bar MUST share that region.

#### Scenario: Countdown is the lower half's only occupant and ticks live
- **Given** Screen 1 renders with `quota_5h.available == true` and a valid `reset_in_seconds`
- **When** the layout is inspected
- **Then** the countdown MUST be the only element positioned in the bottom 50% of screen height
- **And** it MUST recompute every second locally (millis-based), not only on each poll
- **And** each new poll MUST reseed/resync the countdown from the latest `reset_in_seconds`.

#### Scenario: Countdown resumes after reaching zero
- **Given** the countdown reaches `00:00`
- **When** the next dashboard payload provides an updated `reset_in_seconds`
- **Then** the countdown MUST resume counting down from the new value.

#### Scenario: Countdown region shows unavailable state instead of a number
- **Given** `quota_5h.available == false` for the active provider
- **When** the screen renders
- **Then** the countdown region MUST render the dimmed "Sin datos" state instead of a numeric countdown.

### Requirement: AMOLED Auto-Dim on Touch Inactivity
The firmware MUST auto-dim the AMOLED panel after a configured period of touch inactivity, and MUST restore full brightness immediately on the next touch event, without disconnecting Wi-Fi or pausing background polling. Dimming MUST be performed via the display controller's brightness command (CO5300 command `0x51`, using the existing `display.setBrightness()`), not via the AXP2101 PMIC: the AXP2101's BLDO1 rail powering the panel is on/off only and has no brightness/dimming register (confirmed in `pmic_axp2101.cpp`), so commanding it would cut power rather than dim. An optional deeper power-saving step MAY additionally power off the AXP2101 rail during prolonged inactivity, but MUST ship disabled by default, since it may also remove power to the touch controller and prevent wake-on-touch.

#### Scenario: Inactivity dims the display
- **Given** no touch event has been received for the configured inactivity timeout
- **When** that timeout elapses
- **Then** the firmware MUST call `display.setBrightness()` (CO5300 cmd `0x51`) to reduce AMOLED brightness
- **And** Wi-Fi and background polling MUST remain fully active.

#### Scenario: Touch restores brightness
- **Given** the display is currently auto-dimmed
- **When** a new touch event is detected
- **Then** the firmware MUST call `display.setBrightness()` to restore full brightness immediately.

#### Scenario: Deeper AXP2101 power-off step ships disabled
- **Given** the optional deeper power-saving step exists in firmware
- **When** the device ships with default configuration
- **Then** that step MUST be disabled by default, and dimming MUST rely solely on `display.setBrightness()`.
