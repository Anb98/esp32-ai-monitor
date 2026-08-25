# Exploration: post-init-improvements

Code review of `init-ai-monitor` against canonical specs.

## Current State

Read all 4 canonical specs (dashboard-api, provider-metrics, firmware-ui, firmware-hardware) and the archived `init-ai-monitor` verify-report/tasks. Backend (Go, stdlib-only, 88.3% coverage) implements: config loader, Claude/Antigravity provider engines (file-based credential parsing + quota calc), upstream status scraper, in-memory cache, HTTP handlers/middleware, main entrypoint with graceful shutdown. Firmware (ESP32-S3 Arduino/LVGL) implements: CO5300 display driver, CST9220 touch, QMI8658 IMU auto-rotation, ES8311 audio alerts, AXP2101 PMIC suspend/wake, LVGL dual-screen UI, network client polling `/api/dashboard` every 30s.

Cross-checked backend credential parsing against REAL Claude Code / Antigravity CLI files present on this dev machine (`~/.claude/.credentials.json`, `~/.claude.json`, `~/.gemini/config/config.json`, `~/.claude/policy-limits.json`, `~/.claude/stats-cache.json`) to verify whether the implementation would actually work against production installs, not just its own mocked test fixtures.

## Affected Areas (prioritized findings, file:line evidence)

### CRITICAL

1. **Credential discovery/schema mismatch vs real CLIs** — `backend/internal/provider/claude.go:81-86` searches `["credentials.json","session.json","config.json",".claude.json"]` inside configDir; real Claude Code stores OAuth at `~/.claude/.credentials.json` (leading dot) with schema `{"claudeAiOauth":{"accessToken","refreshToken","expiresAt"(ms epoch),...}}` — none of the searched filenames/paths/fields match. Real `~/.claude.json` lives at `$HOME` root, not inside `~/.claude/`. `backend/internal/provider/antigravity.go:79-84` has the same problem vs the real `~/.gemini/config/config.json` layout. Docker mounts (`docker-compose.yml:17-19`) reinforce the same wrong path assumption. Impact: on a real machine the backend will almost always report `auth_valid:false, re_login_required:true` for both providers even when the user is fully logged in. Effort: Medium.
2. **No real local source for quota_5h/quota_weekly usage** — confirmed by inspecting `~/.claude/policy-limits.json` (restrictions only) and `~/.claude/stats-cache.json` (historical token counts, not live plan-quota %). Engines only read `quota_5h`/`quota_weekly` from the (never-present) mocked credential JSON, else fabricate `Used=0` + a synthetic reset time. The dashboard's headline feature (real quota %) is not wired to any real data source. Effort: Large / needs research (open question).
3. **`FetchGoogleStatus` permanent stub** — `backend/internal/provider/status_scraper.go:100-105` always returns `lastGoogleStat`, initialized once to `StatusOperational` and never updated by any network check. Contradicts REQ-PROV-004. Its only test (`status_scraper_test.go:99-105`) merely asserts the value is one of the 3 valid enum strings, so it can never fail despite the function doing nothing. Effort: Medium.
4. **LVGL called from two FreeRTOS tasks with zero synchronization** — `firmware/src/main.cpp:19-25` runs `lv_timer_handler()` in `lvglTask` (core 1, every 5ms); `firmware/src/network_client.cpp:105-109` calls `ui.updateClaude/updateAntigravity` directly from `networkTask` (core 0), which call `lv_label_set_text`/`lv_bar_set_value`/etc. (`ui_manager.cpp:246-289`) with no mutex anywhere in the repo (grepped `mutex`/`semaphore`/`LV_USE_OS` — zero hits). LVGL is documented as not thread-safe; this is a real race condition risking UI corruption/crashes. Effort: Small-Medium (shared FreeRTOS mutex around `lv_timer_handler` and all external LVGL calls, or a queue).
5. **Touch driver out-of-bounds read** — `firmware/src/touch_cst9220.cpp:41-52`: `uint8_t data[6]` (valid idx 0-5), only 6 I2C bytes requested, but `data[6]` is read for the low byte of `rawY` (line 52). Genuine buffer over-read; the Y touch coordinate is computed from garbage stack memory on real hardware. Effort: Small (request 7 bytes, fix index math).

### MEDIUM

6. **Audio blocks the network task ~550-600ms per status transition** — `audio_es8311.cpp:105` uses `i2s_write(..., portMAX_DELAY)`; `playDegradationAlert`/`playRecoveryChime` (110-124) chain tones + `vTaskDelay`, called synchronously from `network_client.cpp:47-52` inside `networkTask`. Contradicts tasks.md 5.6's explicit "non-blocking audio tone generator" requirement. Effort: Small-Medium (own task/queue).
7. **Coverage below the >=85% file-level target** set in tasks.md 2.6 — `status_scraper.go` at 74.5% (verify-report.md line 99), mostly because the Google-status stub has no real branches to test. Fixing #3 first makes this meaningful.
8. **CO5300 TE (tearing-effect) pin defined** (`config.h:17`, GPIO18) but never referenced anywhere in `firmware/src` — no VSYNC/TE sync gating the flush write; the tearing-free claim in verify-report is unverified by any actual sync mechanism.
9. **`PMIC_IRQ_PIN`/`IMU_INT1_PIN` defined but unused** — both button and IMU are pure-polled (20ms loop); dead config, missed power-saving opportunity.
10. **No `.gitignore` anywhere in the repo** — `backend/coverage`/`backend/coverage.out` (generated artifacts) are present in the tree; PlatformIO's `.pio/` build dir would also get committed.
11. **WiFi credentials meant to be hardcoded** into `firmware/include/config.h` per README.md:36, with no `-D WIFI_SSID=...` wiring in platformio.ini and no gitignored secrets pattern — combined with #10, a user following the README literally would likely commit a real Wi-Fi password.

### LOW

12. `backend/internal/api/handler.go:30` silently discards JSON encode errors (`_ = json.NewEncoder(w).Encode(data)`) — inherent net/http limitation post-`WriteHeader`, not really fixable, informational only.
13. `TokenWatcher.PollOnce`'s synthetic per-engine error fallback (`token_watcher.go:31-39`) is dead code — neither engine's `FetchMetrics` ever returns a non-nil error even on read/parse failure, so this path is untested and unreachable as currently written.

## Approaches

1. **Fix real-world provider integration first (findings 1-3)** — Pros: restores the app's actual purpose (real auth/quota/status), highest user value. Cons: requires research into undocumented quota data sources; may need a design/spec update (delta spec) before implementation. Effort: Large.
2. **Fix firmware reliability bugs first (findings 4-6)** — Pros: small, well-understood diffs, prevents crashes/corruption on real hardware. Cons: cannot be unit-tested (no firmware CI here), relies on code review + manual on-device verification. Effort: Small-Medium.
3. **Tooling/hygiene pass only (findings 8-11)** — Pros: trivial, no behavior risk. Cons: does not address the functional gaps that matter most.

## Recommendation

Do NOT start implementation yet — scope is ambiguous on priority and on how much of #1/#2 (real credential/quota integration) the user actually wants solved now vs. deferred. Recommend the orchestrator present the open questions below to the user before running `sdd-propose`, since the answer to "what is the real quota data source" materially changes the size and shape of any proposal.

## Risks

- Findings 1-3 mean the backend may be functionally non-working against real accounts despite passing all its own tests (tests use a mocked schema that does not match reality) — this is a testing-validity risk, not just a feature gap.
- Firmware fixes (4-6) cannot be verified by `go test`/CI in this repo; they rely on static reasoning + manual hardware testing only.
- The real Antigravity/Google status-check data source is unresearched; it may require external API research before scoping.

## Ready for Proposal

No — pending user answers to the open questions (real quota/auth data source, Google status source, firmware-in-scope decision, and priority ordering) before `sdd-propose` can scope a concrete change.

## Open Questions for the User

1. **Real credential parsing**: current Claude/Antigravity discovery is built against a mocked schema that does not match the real CLI file layouts on disk (confirmed via this machine's actual `~/.claude/.credentials.json` and `~/.gemini/config/`). Should this change implement real-world parsing (auth validity + quota), or keep that as a separate/future change?
2. **Quota data source**: neither CLI persists live 5h/weekly plan-quota percentages locally. Is there a real endpoint/file for this (internal API, console API, etc.), or should quota bars be explicitly scoped as "not wired to real usage yet" for now?
3. **Google/Antigravity status**: should `FetchGoogleStatus` be implemented against a real status source in this change, or is the permanent "operational" stub acceptable near-term?
4. **Firmware scope**: several real bugs (touch buffer over-read, LVGL cross-task race, blocking audio, unused TE pin) cannot be unit-tested here (no hardware-in-the-loop CI) — only reasoned statically and confirmed by a clean `pio run` compilation + manual on-device testing. Include firmware fixes in this change (relying on review + manual verification), or defer/track them separately from backend work?
5. **Priority**: rank (a) real auth/quota/status integration, (b) firmware reliability bugs, (c) tooling hygiene (.gitignore, coverage-file cleanup, secrets pattern) — effort/risk vary widely and it is unclear which matters first.

---

_Source: Engram observation #1443, topic `sdd/post-init-improvements/explore`. Materialized to the filesystem during the propose phase (hybrid artifact store)._
