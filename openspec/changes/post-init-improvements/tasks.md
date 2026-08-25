# Tasks: Post-Init Improvements

## Review Workload Forecast

| Field | Value |
|-------|-------|
| Estimated changed lines | ~2000-2800 (new `creds.go`/`authprobe.go` + tests + fixtures, backend wiring, firmware lock/layout/audio rewrite) |
| 400-line budget risk | High |
| Chained PRs recommended | Yes |
| Suggested split | Single PR (`size:exception`), work units below kept as informational rollback slices only |
| Delivery strategy | single-pr |
| Chain strategy | size-exception |

Decision needed before apply: Yes
Chained PRs recommended: Yes
Chain strategy: size-exception
400-line budget risk: High

### Suggested Work Units

| Unit | Goal | Likely PR | Focused test command | Runtime harness | Rollback boundary |
|------|------|-----------|----------------------|-----------------|-------------------|
| 1 | Credentials + auth probe + D5 | single (slice A) | `go test ./backend/internal/provider/... -run "Creds\|AuthProbe\|TokenWatcher"` | N/A - unit tests only | Revert `creds.go`, `authprobe.go`, `claude.go`, `antigravity.go`, `token_watcher.go` |
| 2 | Quota timebox + Google status + dashboard API + docker | single (slice B) | `go test ./backend/...` | `curl localhost:<port>/api/dashboard` via `docker compose up` | Revert `status_scraper.go`, dashboard handler wiring, `docker-compose.yml` |
| 3 | Firmware core fixes (lock/overlay/touch/audio) | single (slice C) | N/A - no firmware unit tests | `pio run` + grep gates | Revert `ui_manager.*`, `main.cpp`, `network_client.cpp`, `touch_cst9220.cpp`, `audio_es8311.cpp` |
| 4 | Firmware UI (D17/countdown/stale/dim) + secrets + verification | single (slice D) | N/A | User on-device checklist | Revert layout/`config.h`/`platformio.ini`/secrets/`.gitignore` |

## Phase 1: Backend Contract Foundation

- [ ] 1.1 Add additive fields to `backend/internal/model/dashboard.go`: `QuotaWindow.Available`, `.ResetInSeconds`; `Provider.AuthState`, `.AuthCheckedAt`, `.Stale`. [dashboard-api: JSON Payload Schema]
- [ ] 1.2 Create `backend/internal/provider/testdata/`: real Claude `.credentials.json` (valid/expired/malformed/truncated/unknown-layout), real Gemini `oauth_creds.json`/`config.json`/`settings.json` variants. [provider-metrics: Credential Ingestion]

## Phase 2: Credential Parsing (test-first, per provider)

- [ ] 2.1 RED `backend/internal/provider/creds_test.go`: real-path resolution incl. env overrides, ms-epoch `expiresAt`, unknown-layout -> `unreadable` (never `absent`).
- [ ] 2.2 GREEN create `backend/internal/provider/creds.go`: ordered candidate probe, tri-state parse, streaming scan of `$HOME/.claude.json`, ms-epoch handling.
- [ ] 2.3 Modify `claude.go`/`antigravity.go` to use `creds.go`; delete fabricated `Used=0`/synthetic reset. [provider-metrics: Credential Ingestion, Quota Metrics]

## Phase 3: Auth Probe + D5 Classification (test-first)

- [ ] 3.1 RED `backend/internal/provider/authprobe_test.go` (`httptest`): 200->valid, 401->expired, 403->unknown+stale, timeout/5xx/refused/429->preserve local-expiry verdict+stale:true (never unknown), on-disk expiry past->expired regardless of probe.
- [ ] 3.2 GREEN create `authprobe.go`: D3 Claude probe (`POST /v1/messages/count_tokens`), D4 Gemini probe (`GET /oauth2/v3/tokeninfo`), D5 classifier, D6 TTL 5min + backoff 30s->10min + mtime re-probe, shared client, `io.LimitReader(64KiB)`. [provider-metrics: Authentication Validity]
- [ ] 3.3 RED+GREEN `token_watcher.go` error fallback: stub-engine error -> `auth_state:"unknown"`, `re_login_required:false`.

## Phase 4: Quota Research Timebox (4h)

- [ ] 4.1 Timebox 4h: verify `anthropic-ratelimit-unified-*` headers on D3's OAuth `count_tokens` response. Present -> 4.2; absent/inconclusive -> ship `available:false` unconditionally, skip 4.2.
- [ ] 4.2 (if found) RED then GREEN `quotaFromRateLimitHeaders` in `authprobe.go`: headers present -> `available:true` + `reset_timestamp`/`reset_in_seconds`; absent -> `available:false`, no fabricated percentage. [provider-metrics: Quota Metrics; dashboard-api: JSON Payload Schema]

## Phase 5: Real Google Status (stub-proof)

- [ ] 5.1 RED `status_scraper_test.go`: fixture Atom feeds (operational/degraded/outage/malformed/non-200) + `atomic.Int32` request counter asserted `==1`.
- [ ] 5.2 GREEN modify `status_scraper.go`: real `FetchGoogleStatus` via `xml.Decoder` under `io.LimitReader(256KiB)`; documented component mapping; constructor `NewStatusScraperWithURLs(claudeURL, googleURL, client)`. [provider-metrics: Upstream Status Scraping]

## Phase 6: Dashboard API Integration + Docker

- [ ] 6.1 RED `backend/internal/api` golden-JSON test: additive fields present, legacy fields byte-identical; `auth_state:"unknown"` golden asserts `auth_valid:false` AND `re_login_required:false`.
- [ ] 6.2 GREEN wire Phases 1-5 into `/api/dashboard`. [dashboard-api: JSON Payload Schema]
- [ ] 6.3 Modify `docker-compose.yml`: `HOME=/root`, mount `${HOME}/.claude.json:/root/.claude.json:ro`, keep `.claude`/`.gemini` mounts.
- [ ] 6.4 Extend RSS `MemStats` test for new streaming paths; assert still < 10MB.

## Phase 7: Firmware Core Fixes

- [ ] 7.1 Modify `ui_manager.h`/`.cpp`: add `uiLock()`/`uiUnlock()`; methods never lock internally.
- [ ] 7.2 Modify `main.cpp`: create mutex before `ui.init()`; lock around `lv_timer_handler()`; spawn `audioTask`.
- [ ] 7.3 Modify `network_client.cpp`: lock around every `ui.update*()` call (no HTTP/JSON/log/delay inside lock).
- [ ] 7.4 Modify `ui_manager.cpp:285` overlay condition to `if (data.reLoginRequired)` only - delete `|| !data.authValid` (D16, binding); `unknown`/`stale` route to `SIN CONEXION`, never overlay. [firmware-ui: Re-Login Required Warning Overlay]
- [ ] 7.5 Modify `touch_cst9220.cpp`: request 7 bytes, read `data[7]` in-bounds. [firmware-hardware: CST9220 Driver]
- [ ] 7.6 Modify `audio_es8311.cpp`: 2-slot queue + `audioTask`; `play*()` -> non-blocking `xQueueSend(...,0)`; `i2s_write` timeout 200ms. [firmware-hardware: ES8311 Status Alerts]

## Phase 8: Firmware UI Additions

- [ ] 8.1 Modify `buildProviderScreen` per D17: title/pill/5h/weekly block moves; add stale badge (R -28, y44).
- [ ] 8.2 Add transparent 480x225 countdown container `set_pos(0,225)`: caption + `HH:MM:SS`/`Nd HH:MM`, seeded from `reset_in_seconds`, ticked by one `lv_timer_create(cb,1000)`, resynced per poll; both unavailable -> dimmed `"Sin datos"`. [firmware-ui: Large Live Reset Countdown]
- [ ] 8.3 Add unavailable-quota rendering: dimmed/grey bar + literal `"Sin datos"` when `available==false`. [firmware-ui: Visual Status Pill and Quota Elements]
- [ ] 8.4 Add stale/offline indicator via `millis()` age vs `STALE_AFTER_MS`; clears on fresh payload. [firmware-ui: Stale/Disconnected Indicator]
- [ ] 8.5 Modify `pmic_axp2101.cpp`: `updateAutoDim()` from `sensorsTask`; dim via `display.setBrightness()` (CO5300 `0x51`) only; deeper `enableAMOLED(false)` step ships disabled. [firmware-ui: AMOLED Auto-Dim]
- [ ] 8.6 Modify `config.h`: `AUTO_DIM_MS`, `AUTO_SLEEP_MS` (default 0), `DIM_LEVEL`, `STALE_AFTER_MS` (30000).

## Phase 9: Firmware Secrets + Tooling

- [ ] 9.1 Modify `platformio.ini`: `extra_configs = secrets.ini`; add `-DLV_FONT_MONTSERRAT_48=1`.
- [ ] 9.2 Create `firmware/secrets.ini.example`; create/extend root `.gitignore` (`.pio/`, `backend/coverage*`, `firmware/secrets.ini`). [firmware-hardware: Build-Time Wi-Fi Credential Injection]

## Phase 10: Final Verification

- [ ] 10.1 `go test ./...` green.
- [ ] 10.2 `status_scraper.go` coverage >= 85% (`go test -cover`).
- [ ] 10.3 Backend RSS < 10MB (`MemStats` check).
- [ ] 10.4 `pio run` clean build.
- [ ] 10.5 Grep gates: no `authValid` in any UI visibility branch (D16); no widget at y>=225 outside the countdown container (D17); no `lv_` call in `network_client.cpp` outside the lock; `data[7]` with 7-byte request; no `portMAX_DELAY` in `i2s_write`; no `audio.play*` on the network task.
- [ ] 10.6 User on-device checklist: touch accuracy, countdown drift/resync, dim/sleep thresholds, alert timing (no repeat, no poll block), no UI stall under poll+swipe, overlay only on confirmed expiry.

## Dependencies

Backend (1-6) and firmware (7-9) are independent after 1.1 lands. Backend: 2->3->4->5->6 sequential. Firmware: 7.1->7.2->7.3 sequential (lock primitive before callers); 7.4-7.6 parallel with each other and with 8.x once 7.1-7.3 land; 8.1->8.2->8.3 sequential; 8.4-8.5 parallel. Phase 9 parallel with anything. Phase 10 last.
