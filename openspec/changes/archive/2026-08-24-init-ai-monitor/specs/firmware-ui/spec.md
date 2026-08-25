# Capability: firmware-ui

## Overview
The `firmware-ui` capability specifies the graphical user interface rendered on the 2.16" 480x480 AMOLED display via LVGL (v8 or v9). It provides a dual-view architecture for Claude Code and Google Antigravity, swipe gesture navigation, dynamic status badges, quota progress indicators with reset times, and proactive re-login alerts.

---

## Requirements

### REQ-UI-001: Dual-Screen Layout & Resolution
The UI engine MUST instantiate two primary dashboard views rendered at 480x480 native resolution:
1. **Screen 1**: Claude Code dashboard view (`id: claude`).
2. **Screen 2**: Antigravity dashboard view (`id: antigravity`).

#### Scenario: Initial screen loading
- **Given** the ESP32-S3 boots and initializes LVGL
- **When** the UI layout engine starts
- **Then** Screen 1 (Claude Code) MUST be rendered as the default active screen.

---

### REQ-UI-002: Full-Screen Horizontal Swipe Navigation
The UI MUST allow switching between Screen 1 and Screen 2 exclusively using horizontal touch swipe gestures (left and right). The interface SHALL NOT require or display on-screen navigation buttons.

#### Scenario: Swipe left to transition to Screen 2
- **Given** Screen 1 (Claude Code) is currently visible
- **When** the user performs a horizontal swipe gesture from right to left
- **Then** the UI MUST transition smoothly to Screen 2 (Antigravity) using an animated screen slide transition.

#### Scenario: Swipe right to transition to Screen 1
- **Given** Screen 2 (Antigravity) is currently visible
- **When** the user performs a horizontal swipe gesture from left to right
- **Then** the UI MUST transition smoothly to Screen 1 (Claude Code).

---

### REQ-UI-003: Page Indicator Dots
The UI MUST render subtle page indicator dots centered at the bottom of the screen to indicate the active view index:
- Screen 1 active: `● ○` (first dot filled, second dot outline/dimmed).
- Screen 2 active: `○ ●` (first dot outline/dimmed, second dot filled).

#### Scenario: Page indicator updates on screen switch
- **Given** the display is currently on Screen 1 showing `● ○`
- **When** the user swipes to Screen 2
- **Then** the bottom indicator MUST immediately update to `○ ●`.

---

### REQ-UI-004: Visual Status Pill and Quota Elements
Each provider screen MUST render the following visual elements in a cohesive 480x480 dark-mode theme:
1. **Header**: Provider name and logo icon.
2. **Status Pill**:
   - 🟢 `"Operativo"` / `"Operational"` with green accent when `status == "operational"`.
   - 🟡 `"Degradado"` / `"Degraded"` with yellow/amber accent when `status == "degraded"`.
   - 🔴 `"Interrupción"` / `"Outage"` with red accent when `status == "outage"`.
3. **5-Hour Quota Progress Bar**:
   - Horizontal progress bar representing 0% to 100% consumption.
   - Text label with percentage (e.g. `"42.5%"`) and reset time countdown (e.g. `"Reset: 15:30"`).
   - Dynamic bar color (Green for <70%, Yellow for 70-90%, Red for >90%).
4. **Weekly Quota Progress Bar**:
   - Horizontal progress bar representing 0% to 100% weekly consumption.
   - Text label with percentage (e.g. `"30.0%"`) and reset day/time (e.g. `"Reset: Dom 00:00"`).

#### Scenario: Quota bar color shifts above threshold
- **Given** the 5-hour quota usage reaches 92.0%
- **When** the dashboard state updates from the backend payload
- **Then** the progress bar fill color MUST transition to red
- **And** the text label MUST display `"92.0%"` alongside the updated reset time.

---

### REQ-UI-005: Re-Login Required Warning Overlay
When `re_login_required` is `true` or `auth_valid` is `false` for an active provider, the UI MUST display a prominent warning overlay card over the quota metrics area:
- Icon: 🔑
- Title: `"RE-LOGIN REQUERIDO"`
- Instructions: `"Ejecuta 'claude' o 'agy login' en terminal"`

#### Scenario: Display warning card on auth expiration
- **Given** the ESP32 receives a dashboard payload with `claude.re_login_required == true`
- **When** Screen 1 (Claude Code) renders
- **Then** the UI MUST display the re-login warning card prominently in the center of Screen 1.

#### Scenario: Automatic dismissal of warning card
- **Given** the warning card is currently visible on Screen 1
- **When** the next dashboard payload reports `claude.re_login_required == false` and `claude.auth_valid == true`
- **Then** the warning card MUST be smoothly hidden and the regular quota bars MUST be displayed.
