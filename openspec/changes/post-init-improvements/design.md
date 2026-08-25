# Design: Post-Init Improvements

## Technical Approach

Truth over shape: every value is either measured or flagged unavailable. Backend gains a tri-state auth verdict and an `available` flag per quota window; the firmware gets one LVGL lock, an audio queue, and a bounded touch read. No new dependencies (Go stdlib only; firmware reuses LVGL/FreeRTOS primitives already linked). Backend concurrency is unchanged: engines stay owned by the single `TokenWatcher` goroutine, so the new per-engine cache state needs no locks.

## Architecture Decisions

| # | Decision | Choice | Rejected | Rationale |
|---|---|---|---|---|
| D1 | Credential discovery | Ordered candidate probe per provider; first parse yielding a token wins; tri-state result `found / absent / unreadable` | Single hardcoded path | Schemas vary by CLI version and OS; unknown layout must degrade, never assert logged-out |
| D2 | Auth verdict | `auth_state: valid \| expired \| unknown`; legacy `auth_valid = (state==valid)`, `re_login_required = (state==expired)`; credentials absent ⇒ `expired`, unreadable ⇒ `unknown` | Keep two booleans | `unknown` is what prevents a false re-login modal; additive per proposal decision 6 |
| D3 | Claude probe | `POST https://api.anthropic.com/v1/messages/count_tokens` with `Authorization: Bearer <accessToken>` | Refresh-token grant (rotates the user's refresh token — can log them out); `/v1/messages` (burns quota) | Real authenticated call at zero token cost. It is also the **candidate** carrier of the unified rate-limit headers D7 wants — plausible, not verified (Open Questions 1–2, 4 h timebox, `available:false` on absence) |
| D4 | Gemini probe | `GET https://www.googleapis.com/oauth2/v3/tokeninfo?access_token=…` | Vertex/CloudCode calls (need project + billing context) | Google's own documented validation endpoint; free; 200 = valid, 400 = expired |
| D5 | Failure classification | On-disk `expiresAt` already past ⇒ `expired` (authoritative; a probe cannot override it). Otherwise: 200 ⇒ `valid`; 401 ⇒ `expired`; 403 or any other indeterminate response ⇒ `unknown` + `stale:true`; timeout, DNS, TLS, refused, 429, 5xx, malformed body ⇒ **keep the local-expiry verdict** (`valid`) + `stale:true`, `auth_checked_at` unchanged — never `unknown` | Treat any non-200 as invalid; map every probe failure to `unknown` | Network trouble must never read as "you are logged out" (decision 2). `unknown` is reserved for answers that prove neither validity nor expiry, so it never carries a network story |
| D6 | Probe cadence | Inline in `FetchMetrics`, TTL 5 min per provider, 5 s timeout, backoff 30 s→10 min on consecutive failures, re-probe early if the credential file mtime changes | Probe every 30 s poll | Rate-limit safety and RSS: one reused `http.Client`, bodies via `io.LimitReader(64 KiB)` |
| D7 | Quota source | Parse `anthropic-ratelimit-unified-*` headers off the D3 response; if absent ⇒ `available:false` | Synthesizing from `stats-cache.json` | Proposal decision 1 forbids approximations; piggybacking costs zero extra requests |
| D8 | Google status | Stream `https://status.cloud.google.com/en/feed.atom` with `xml.Decoder` under `io.LimitReader(256 KiB)` | `incidents.json` (multi-MB — breaks the 10 MB RSS budget) | Small, stable, stdlib-parseable; token streaming keeps allocation flat |
| D9 | Firmware countdown input | Backend sends `reset_in_seconds` (relative) alongside the absolute `reset_timestamp` | Absolute epoch only | NTP was declined, so the device has **no wall clock** and cannot subtract an epoch. It seeds from the relative value and ticks with `millis()` |
| D10 | LVGL serialization | One non-recursive `SemaphoreHandle_t`; **callers lock at the task boundary**, `UIManager` methods never lock internally | Locking inside `UIManager` | Internal calls nest (`showScreen`→`updateIndicatorDots`) and the touch `read_cb` already runs inside `lv_timer_handler`; internal locking would self-deadlock |
| D11 | Countdown/staleness ticker | One `lv_timer_create(cb, 1000)` | A third FreeRTOS task | LVGL timers run inside `lv_timer_handler`, i.e. already under the lock — no extra task, no extra locking |
| D12 | Audio | 2-slot FreeRTOS queue + `audioTask`; `play*()` becomes a non-blocking `xQueueSend(…, 0)`; `i2s_write` timeout `200 ms` instead of `portMAX_DELAY` | Rewriting to non-blocking i2s writes | Smallest diff that gets ~600 ms off the network task; a wedged codec can no longer stall audio forever |
| D13 | Auto-dim transport | Dim via the existing `display.setBrightness()` (CO5300 cmd `0x51`); AXP2101 `enableAMOLED(false)` only for the deeper off step | Dimming through the AXP2101 | BLDO1 is an on/off rail — it cannot dim an AMOLED. Proposal intent preserved; hardware reality respected |
| D14 | Wi-Fi secrets | `platformio.ini` → `extra_configs = secrets.ini` (gitignored) + committed `secrets.ini.example` | Editing `config.h` per README | Native PlatformIO feature; `config.h`'s `#ifndef` placeholders stay as the no-secrets fallback |
| D15 | New UI strings | ASCII only (`SIN DATOS`, `SIN CONEXION`, `RESET 5H`) | Accented Spanish | The extended-glyph font fix is explicitly declined; accents would render as boxes |
| D16 | Re-login overlay trigger (firmware) | Overlay shows **iff** `re_login_required == true` (≡ `auth_state == "expired"`). `auth_valid == false` alone never triggers it; `unknown` routes to the stale / `SIN CONEXION` presentation | Keep today's `reLoginRequired \|\| !authValid` (`ui_manager.cpp:285`) | Under D2, `unknown` makes `auth_valid` false, so the legacy OR would raise "RE-LOGIN REQUERIDO" on a transient probe failure — exactly what decision 2 forbids. Backend truth is worthless if the consumer re-derives the old lie |
| D17 | Provider-screen layout split | Upper half (y 0–222 of the 450 px tile) keeps title, pill, stale badge and **both** quota blocks at compressed spacing; a transparent 480×225 container pinned at y=225 owns the lower half and holds only the countdown | Fitting the countdown into the leftover space under the weekly bar (today's build runs to y≈281) | Decision 4 makes the countdown the *sole* occupant of the lower half; a container makes that exclusivity structural instead of a coordinate convention future edits can erode |

## Data Flow

    credential files ─┐
                      ├─→ ClaudeEngine ─┬─(D3 probe)→ auth_state + ratelimit headers → quota{available}
    status.claude.com ┘                 │
    feed.atom ────────→ StatusScraper ──┴─→ Provider ─→ TokenWatcher ─→ Cache ─→ GET /api/dashboard
                                                                                        │
                                       firmware: networkTask ←────────────────────────── ┘
                                          │ (lock) update widgets + seed reset_in_seconds, lastSuccessMs
                                          │   auth: expired → overlay; unknown/stale → SIN CONEXION (never overlay)
                                          ├→ audio queue → audioTask (core 1)
                                          └→ lv_timer 1 Hz (inside lock): countdown tick + payload-age check

## File Changes

| File | Action | Description |
|---|---|---|
| `backend/internal/provider/creds.go` | Create | Candidate probing, tolerant tri-state parse, streaming `~/.claude.json` scan, `expiresAt` (ms) handling |
| `backend/internal/provider/authprobe.go` | Create | D3/D4 probes, D5 classification, D6 TTL+backoff, `quotaFromRateLimitHeaders` |
| `backend/internal/provider/{claude,antigravity}.go` | Modify | Use the above; **delete** the synthetic reset-boundary and `Used=0` fabrication |
| `backend/internal/provider/status_scraper.go` | Modify | Real `FetchGoogleStatus`; constructor becomes `NewStatusScraperWithURLs(claudeURL, googleURL, client)` |
| `backend/internal/provider/token_watcher.go` | Modify | Error fallback emits `auth_state:"unknown"`, `re_login_required:false` (was a false re-login) |
| `backend/internal/model/dashboard.go` | Modify | Additive: `QuotaWindow.available`, `.reset_in_seconds`; `Provider.auth_state`, `.auth_checked_at`, `.stale` |
| `backend/internal/provider/testdata/**` | Create | Sanitized real-schema fixtures + malformed/unknown variants |
| `docker-compose.yml` | Modify | Add `HOME=/root`, mount `${HOME}/.claude.json:/root/.claude.json:ro`, keep `.claude`/`.gemini` dirs |
| `firmware/include/ui_manager.h`, `src/ui_manager.cpp` | Modify | `uiLock()/uiUnlock()`; **overlay condition (`ui_manager.cpp:285`) becomes `if (data.reLoginRequired)` — the `\|\| !data.authValid` clause is deleted** (D16); D17 layout move + countdown container; stale badge; unavailable-quota rendering; `ProviderUIData` gains `authState`, `stale`, per-window `available` / `resetIn` |
| `firmware/src/main.cpp` | Modify | Create the mutex before `ui.init()`; lock around `lv_timer_handler`; create `audioTask` |
| `firmware/src/network_client.cpp` | Modify | Lock around `ui.update*`; parse `auth_state` and `stale` plus per-window `available` / `reset_in_seconds` into `ProviderUIData`; record `_lastSuccessMs`; queue audio |
| `firmware/src/touch_cst9220.cpp` | Modify | 7-byte read, `data[7]`, `_lastTouchMs` for auto-dim |
| `firmware/src/audio_es8311.cpp` | Modify | Queue + task; bounded `i2s_write` timeout |
| `firmware/src/pmic_axp2101.cpp` | Modify | `updateAutoDim()` state machine called from `sensorsTask` |
| `firmware/include/config.h` | Modify | `AUTO_DIM_MS`, `AUTO_SLEEP_MS`, `DIM_LEVEL`, `STALE_AFTER_MS` knobs |
| `firmware/platformio.ini` | Modify | `extra_configs = secrets.ini`; `-DLV_FONT_MONTSERRAT_48=1` |
| `firmware/secrets.ini.example`, `.gitignore` | Create | Secrets template; ignore `.pio/`, `backend/coverage*`, `firmware/secrets.ini` |

## Interfaces / Contracts

```go
type QuotaWindow struct {
    Used, Limit, Percentage float64
    ResetTime      string `json:"reset_time"`
    ResetTimestamp int64  `json:"reset_timestamp"`
    Available      bool   `json:"available"`          // NEW: false ⇒ ignore the numbers
    ResetInSeconds int64  `json:"reset_in_seconds"`   // NEW: clockless-device countdown seed
}
type Provider struct { /* … */
    AuthState     string `json:"auth_state"`        // NEW: valid | expired | unknown
    AuthCheckedAt int64  `json:"auth_checked_at"`   // NEW: unix, 0 = never probed
    Stale         bool   `json:"stale"`             // NEW: some part of this report is
}                                                    //      last-known — status feed and/or auth probe (D5)
```

```cpp
struct ProviderUIData { /* … */
    String  authState;                              // NEW: valid | expired | unknown
    bool    stale;                                  // NEW: payload/status is last-known
    bool    quota5hAvailable, quotaWeeklyAvailable; // NEW: false ⇒ SIN DATOS, dimmed bar
    int32_t quota5hResetIn, quotaWeeklyResetIn;     // NEW: seconds, <0 ⇒ unknown
};
```

**Firmware auth consumption** (D16, binding): `network_client.cpp` reads `p["auth_state"]` and `p["stale"]`; `re_login_required` keeps its current parse and becomes the **only** overlay input. `ui_manager.cpp` shows the overlay on `reLoginRequired` alone. `authState == "unknown"` (and `stale`) drive the dimmed/`SIN CONEXION` presentation and must never reach the overlay branch. `authValid` survives only as a legacy field — no UI branch may read it. Firmware `stale = payload "stale" || payload age ≥ STALE_AFTER_MS` (one flag, no second field).

**Provider screen layout** (D17; `buildProviderScreen`, tile 480×450, `LV_ALIGN_TOP_*` offsets):

| Widget | Position | Font |
|---|---|---|
| Title | L 28, y 12 | 24 |
| Status pill 130×28 | R -28, y 10 | 14 |
| Stale badge (new, hidden when fresh) | R -28, y 44 | 14 |
| `Cuota 5 Horas` / pct | L 28, y 62 / R -28, y 58 | 16 / 20 |
| 5h bar 424×18 | MID, y 84 | — |
| 5h reset | L 28, y 104 | 14 |
| `Cuota Semanal` / pct | L 28, y 128 / R -28, y 124 | 16 / 20 |
| Weekly bar 424×18 | MID, y 150 | — |
| Weekly reset | L 28, y 170 | 14 |
| Countdown container 480×225, transparent | `set_pos(0, 225)` | — |
| ↳ caption (`RESET 5H` / `RESET SEMANAL` / `SIN DATOS`) | TOP_MID, y 8 | 16 |
| ↳ value `HH:MM:SS` / `Nd HH:MM` | `lv_obj_center` | 48 |

Moves from today's build: title 28→12, pill 26→10, 5h block 90/122/152→62/84/104, weekly block 205/237/267→128/150/170. Upper half ends at y≤184; nothing but the countdown container exists at y≥225. The re-login overlay stays a centered modal card and is the only widget allowed to cover the lower half.

Real schemas parsed: `~/.claude/.credentials.json` → `claudeAiOauth.{accessToken,expiresAt(ms)}`; `$HOME/.claude.json` → `oauthAccount` (secondary evidence only, stream-scanned — the file can be megabytes); `~/.gemini/oauth_creds.json` → `{access_token, expiry_date(ms)}`, falling back to `~/.gemini/config/config.json` and `~/.gemini/settings.json`.

**Google status mapping** (documented per proposal decision 3): unresolved Atom entry whose title/summary matches `Gemini | Vertex AI | Generative Language` → `outage` if it mentions *outage/unavailable*, else `degraded`; no matching unresolved entry → `operational`; fetch failure or unparseable feed → last known + `stale:true`.

**LVGL lock contract**: `lvglTask` holds it only around `lv_timer_handler()`; `networkTask` holds it only around `ui.update*()` (no HTTP/JSON/log/delay inside); anything invoked from an LVGL callback (touch `read_cb`, tileview event, the 1 Hz timer) must **not** take it. `xSemaphoreTake` uses a 100 ms timeout — on failure the update is skipped, never blocked.

**Firmware staleness/countdown**: both derive from `millis()` deltas (unsigned subtraction is wrap-safe); `age ≥ STALE_AFTER_MS (30 s)` ⇒ dim the tile content and show `SIN CONEXION`. The countdown shows the nearest *available* window (`RESET 5H` / `RESET SEMANAL`), `HH:MM:SS` or `Nd HH:MM`, alone in the D17 container; both unavailable ⇒ `SIN DATOS`. The re-login overlay may cover it — a countdown is meaningless while genuinely logged out.

## Testing Strategy

| Layer | What | Approach |
|---|---|---|
| Unit (RED first) | Credential parsing | `testdata` fixtures with the **real** schemas + truncated/empty/wrong-type/unknown-layout; unknown must yield `unknown`, never `expired` |
| Unit | Auth classification | `httptest` per D5: 200⇒`valid`, 401⇒`expired`, 403⇒`unknown`+`stale`, 500/timeout/refused⇒**local verdict preserved** (`valid`) + `stale:true`, never `unknown`; on-disk expiry past ⇒ `expired` regardless of probe |
| Unit | Quota headers | Headers present ⇒ `available:true` + parsed reset; absent ⇒ `available:false`, percentage 0 |
| Unit | Google status | Fixture Atom feeds (operational/degraded/outage/malformed/non-200) **plus an `atomic.Int32` request counter asserted `== 1`** — deleting the fetch fails the test |
| Unit | TokenWatcher error branch | Stub engine returning an error ⇒ `auth_state:"unknown"`, `re_login_required:false` |
| Integration | `/api/dashboard` | Golden JSON: additive fields present, legacy fields byte-identical; `auth_state:"unknown"` golden asserts `auth_valid:false` **and** `re_login_required:false` |
| Budget | RSS | Existing `MemStats` check; feeds/credentials read through `io.LimitReader` + streaming decoders, never `io.ReadAll` |
| Firmware (static) | Compile + invariants | Clean `pio run`; grep gates: no `lv_` call in `network_client.cpp` outside the lock, `data[7]` with a 7-byte request, no `portMAX_DELAY` in `i2s_write`, no `audio.play*` on the network task, **no `authValid` in any overlay/visibility branch** (D16), no widget placed at y≥225 outside the countdown container (D17) |
| Firmware (manual) | On-device | Touch accuracy, countdown drift, dim/sleep thresholds, alert timing, no UI stall under poll+swipe |

Coverage target `status_scraper.go ≥ 85%` is met by D8's real branches.

## Threat Matrix

N/A — no routing, shell, subprocess, VCS/PR automation, executable-file classification, or process-integration boundary is introduced (probes are plain HTTPS; no CLI is spawned). Two trust boundaries are still handled explicitly: credential files are size-capped and stream-parsed with no reflection into logs, and access tokens are sent only to their own issuer's endpoint and never logged.

## Migration / Rollout

No migration. Schema changes are additive, so an un-updated firmware keeps working (it ignores the new fields; unavailable quotas still read 0) — with one known gap: its legacy `|| !authValid` still raises a false overlay on `unknown`, so the D16 hunk must be flashed for decision 2 to hold on-device. Backend and firmware hunks revert independently.

## Open Questions

- [ ] D7 assumes the `anthropic-ratelimit-unified-*` headers are present on OAuth-authenticated `count_tokens` calls. Timebox: 4 h. Absent ⇒ ship `available:false` (proposal-accepted).
- [ ] D3 scope acceptance for `count_tokens` under an OAuth (non-API-key) token is unverified. A scope-driven 403 proves neither validity nor expiry, so D5 already routes it to `unknown` (`stale:true`), which D16 keeps out of the overlay. If 403 turns out to be the *normal* answer, revisit D4-style validation for Claude.
- [ ] D13's deep-off step may cut power to the touch controller; if so the only wake path is the BOOT button. Gate it behind `AUTO_SLEEP_MS=0` (disabled) until the on-device check confirms.
