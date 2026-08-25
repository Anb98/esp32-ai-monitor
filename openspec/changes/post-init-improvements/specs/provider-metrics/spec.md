# Delta for provider-metrics

## MODIFIED Requirements

### Requirement: Credential Ingestion and Path Resolution
The provider metrics engine MUST discover and load credentials from the REAL on-disk locations and schemas used by the installed CLIs, not an invented layout:
1. Claude Code: OAuth token at `~/.claude/.credentials.json` (or `$CLAUDE_CONFIG_DIR/.credentials.json`), JSON shape `{"claudeAiOauth":{"accessToken","refreshToken","expiresAt", ...}}` where `expiresAt` is a Unix epoch in **milliseconds**; account/session metadata at `$HOME/.claude.json` (at `$HOME` root, NOT inside `~/.claude/`).
2. Google Antigravity: config directory `~/.gemini/config/` (or `$GEMINI_CONFIG_DIR/config/`), primarily `config.json`; the engine MUST parse the actual directory/file layout of the real Gemini/Antigravity CLI install, not a name it invents.
(Previously: searched `["credentials.json","session.json","config.json",".claude.json"]` inside `~/.claude`, matching no real CLI file; `~/.gemini/credentials.json` likewise did not exist.)

#### Scenario: Custom mount path resolution
- **Given** `CLAUDE_CONFIG_DIR=/root/.claude` and `GEMINI_CONFIG_DIR=/root/.gemini` are set
- **When** the engine initializes
- **Then** it MUST resolve `/root/.claude/.credentials.json` and `/root/.gemini/config/config.json`.

#### Scenario: Real Claude OAuth schema and epoch units
- **Given** `~/.claude/.credentials.json` exists with `claudeAiOauth.expiresAt` as a millisecond epoch
- **When** the engine computes token expiry
- **Then** it MUST treat `expiresAt` as milliseconds (not seconds), and MUST read `$HOME/.claude.json`, not `~/.claude/.claude.json`.

#### Scenario: Default user home directory fallback
- **Given** no override environment variables are set
- **When** the engine reads credentials
- **Then** it MUST resolve `$HOME/.claude/.credentials.json` and `$HOME/.gemini/config/config.json`.

---

### Requirement: Authentication Validity and Re-Login Detection
The engine MUST validate credentials via a real authenticated API call (not file presence alone) and set `auth_valid`, `re_login_required`, and `stale`:
1. Missing/unreadable file -> `auth_valid=false`, `re_login_required=true`.
2. Empty/malformed JSON -> `auth_valid=false`, `re_login_required=true`.
3. Authenticated call returns 401/403 -> `auth_valid=false`, `re_login_required=true`.
4. Authenticated call succeeds -> `auth_valid=true`, `re_login_required=false`, `stale=false`.
5. Authenticated call fails on network/timeout error (not 401/403) -> MUST NOT set `re_login_required=true`; MUST fall back to a local `expiresAt` check: not-yet-expired -> `auth_valid=true`, `stale=true`; expired -> `auth_valid=false`, `re_login_required=true`.
(Previously: validity was inferred without a live call; any API failure was indistinguishable from a real 401.)

#### Scenario: Missing credential file
- **Given** no credential file exists
- **When** metrics collection runs for `"claude"`
- **Then** `auth_valid: false` and `re_login_required: true`, with zeroed metrics and no crash.

#### Scenario: Real call actually invoked before declaring validity
- **Given** valid-looking credentials on disk
- **When** metrics collection runs
- **Then** the engine MUST issue a live authenticated request to the provider before setting `auth_valid`.

#### Scenario: Network failure degrades to local expiry, never false re-login
- **Given** locally unexpired credentials AND the authenticated validity call times out
- **When** metrics collection runs
- **Then** `auth_valid` MUST remain `true`, `stale` MUST be `true`, and `re_login_required` MUST remain `false`.

---

### Requirement: Upstream Status Scraping & Component Filtering
The engine MUST monitor and map provider status into `"operational"|"degraded"|"outage"`:
1. **Claude Code**: unchanged — query `status.claude.com`, filter component `yyzkbfz2thpt`, map as before.
2. **Google Antigravity**: the engine MUST query Google's public status/incident feed for the Gemini API and evaluate the health of the specific component representing the "Gemini (Flash)" model, with that component selection documented in code (constant/comment). No active incident on that component -> `"operational"`; a low-severity/informational incident -> `"degraded"`; a high-severity/outage incident -> `"outage"`. The result MUST reflect the current query, not a value fixed at startup.
(Previously: `FetchGoogleStatus` was a permanent stub returning a value set once at init and never updated.)

#### Scenario: Specific Claude Code component degradation
- **Given** Anthropic's general UI has an outage but component `yyzkbfz2thpt` reports `"operational"`
- **When** status polling executes
- **Then** Claude status MUST report `"operational"`.

#### Scenario: Google status varies with the live feed
- **Given** two successive polls where the Gemini (Flash) status feed reports first an active incident, then no incident
- **When** each poll executes
- **Then** the two returned Antigravity statuses MUST differ accordingly (a function returning the same value regardless of feed state FAILS this scenario).

#### Scenario: Network failure during status scrape
- **Given** the upstream status endpoint is unreachable
- **When** the status poll occurs
- **Then** the engine MUST NOT crash and MUST retain the last known valid status with a logged warning.

---

### Requirement: Quota Metrics Calculation & Window Formatting
The engine MUST compute rolling-window usage when a live usage-data source exists, and MUST explicitly mark unavailability when it does not — never a fabricated `0`:
1. **5-Hour (`quota_5h`)** and **Weekly (`quota_weekly`)**: when a live source provides data, populate `used`, `limit`, `percentage`, `reset_time`, `reset_timestamp` as before.
2. When no live usage-data source exists: the engine MUST set `available: false` on the affected quota object and MUST NOT populate `used`/`percentage` as if they were a real reading.
(Previously: absent a live source, `used` was fabricated as `0` with a synthetic reset time, indistinguishable from real zero usage.)

#### Scenario: Quota percentage calculation
- **Given** a 5-hour limit of 100.0 and usage of 42.5
- **When** metrics are computed
- **Then** `percentage` MUST be `42.5` and `reset_time` a non-empty `HH:MM` string.

#### Scenario: No live source -> explicit unavailable, not zero
- **Given** no live usage-data source is reachable for a provider
- **When** metrics are computed
- **Then** `quota_5h.available` and `quota_weekly.available` MUST be `false`, and the payload MUST be distinguishable from a real `used=0` reading.
