# Proposal: Post-Init Improvements

## Intent

`init-ai-monitor` passes its own tests against fixtures that do not match reality. On a real machine: credential paths/schemas match no CLI, so both providers report `re_login_required` while the user is logged in; quota bars show fabricated `Used=0`; `FetchGoogleStatus` is a stub. Firmware carries three defects that still compile: an unsynchronized LVGL cross-task race, a 1-byte out-of-bounds touch read, and audio blocking the network task ~600ms. Success: the device shows truthful data or an honest "unknown", and the firmware stops racing and over-reading.

## Scope

### In Scope
- Real credentials: `~/.claude/.credentials.json` (`claudeAiOauth.expiresAt`, ms epoch), `$HOME/.claude.json`, real `~/.gemini/config/`; fix `docker-compose.yml` mounts.
- Investigate a live-quota source; wire it if found, else expose an explicit "usage unavailable" state end-to-end instead of `Used=0`.
- Real `FetchGoogleStatus`, plus a test that fails if the fetch is removed.
- Firmware: LVGL mutex over `lv_timer_handler` and every cross-task LVGL call; 7-byte touch read with fixed indexing; audio off the network task.
- Firmware UI additions (user-approved): stale-data/disconnection indicator (payload age, offline state after ~30s without payload), large live reset countdown occupying the lower half of each provider screen, AMOLED auto-dim after touch inactivity via the AXP2101.
- Tooling: `.gitignore` (`.pio/`, `backend/coverage*`); Wi-Fi credentials via build flags + gitignored secrets file.
- Follow-on: `TokenWatcher` error branch becomes reachable/tested; `status_scraper.go` coverage back over 85%.

### Out of Scope
- CO5300 TE/VSYNC sync (GPIO18 unused): no reported tearing, no symptom to chase.
- Interrupt-driven PMIC/IMU: polling works; optimization without measured need.
- `handler.go` discarded encode error: net/http limitation post-`WriteHeader`.
- UI beyond the listed additions (user-declined): extended-glyph fonts (accented "Interrupción"), NTP clock, incident-detail line, active-model/session-cost widgets (backend cannot observe sessions on other machines).
- New endpoints, additional providers.

## Capabilities

### New Capabilities
- None.

### Modified Capabilities
- `provider-metrics`: REQ-PROV-001 real paths/schemas; -002 validity via a real authenticated call (network failure degrades to local expiry + staleness, never a false re-login); -004 real Google status mapped from Gemini (Flash) model health on Google's status feed, mapping documented; -005 quota may be unavailable, not zero.
- `dashboard-api`: REQ-DASH-005 additive fields distinguishing "no usage data" from 0% and machine-readable quota reset timestamps for the firmware countdown.
- `firmware-ui`: REQ-UI-004 unavailable-quota rendering (dimmed bars + explicit "Sin datos" label); new requirements: serialized LVGL access, stale/disconnected indicator, large lower-half reset countdown, AMOLED auto-dim on inactivity.
- `firmware-hardware`: REQ-HW-004 alerts must not block polling; new build-time Wi-Fi credential injection requirement.

## Approach

Backend first (credentials -> quota research -> status): it defines the JSON contract the firmware consumes. Stdlib-only, <10MB RSS, strict TDD. Firmware fixes land after the contract settles. Quota research is timeboxed; finding no source means shipping the honest unavailable state, never a fabricated number.

## Affected Areas

| Area | Impact | Description |
|------|--------|-------------|
| `backend/internal/provider/*.go` | Modified | Real paths/schemas, expiry validity, real Google status, real errors |
| `backend/internal/api/`, `docker-compose.yml` | Modified | Quota-availability field; corrected mounts |
| `firmware/src/{main,ui_manager,network_client}` | Modified | LVGL mutex; stale/disconnect indicator; lower-half reset countdown; auto-dim (AXP2101) |
| `firmware/src/{touch_cst9220,audio_es8311}.cpp` | Modified | In-bounds read; non-blocking playback |
| `.gitignore`, `platformio.ini`, `firmware/include/` | New/Modified | Ignores; Wi-Fi secrets injection |

## Risks

| Risk | Likelihood | Mitigation |
|------|------------|------------|
| No real quota source exists | High | Accepted: ship explicit unavailable state |
| CLI schemas vary by version/OS | Med | Tolerant parsing, degrade to unavailable, never crash |
| Firmware unverifiable in CI | High | Static review + clean `pio run` + user on-device test |
| Mutex misuse deadlocks the UI | Med | One lock, bounded hold, no nesting |

## Rollback Plan

Single revertible PR. Backend and firmware hunks are independent and can be reverted separately. No migrations, no persisted state, no external side effects.

## Dependencies

- Logged-in Claude Code / Antigravity CLIs for schema verification.
- Physical ESP32-S3 board for the user's manual firmware check.

## Success Criteria

- [ ] Logged-in CLI yields `auth_valid: true` for both providers.
- [ ] Quota shows real usage or an explicit unavailable state; never fabricated zeros.
- [ ] `FetchGoogleStatus` does a real check; its test fails without the network call.
- [ ] `go test ./...` green, `status_scraper.go` >= 85%, RSS < 10MB.
- [ ] `pio run` clean; no LVGL call outside the mutex; touch read in bounds; alerts do not stall polling.
- [ ] UI marks data stale/disconnected within ~30s of backend loss; countdown and auto-dim behave correctly in the user's on-device check.
- [ ] Following the README cannot commit credentials or build artifacts.

## Post-Proposal Decisions (2026-08-24, user-approved)

1. Quota with no live source: dimmed bars with an explicit "Sin datos" label; no approximations from historical stats.
2. Auth validity: real authenticated call; on network failure degrade to local expiry check + staleness marker, never a false "re-login required".
3. Antigravity status: mapped from Gemini (Flash) model health on Google's status feed, with the mapping documented.
4. UI scope additions: stale/disconnection indicator, large lower-half reset countdown (sole occupant of the lower half), AMOLED auto-dim via AXP2101.
5. Declined: extended-glyph font fix, NTP clock, incident-detail line, active-model/session-cost widgets.
6. Standing assumptions confirmed by silence: Docker deployment stays supported (mounts corrected); `dashboard-api` schema changes are additive only.
