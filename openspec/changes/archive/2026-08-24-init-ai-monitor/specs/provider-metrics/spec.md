# Capability: provider-metrics

## Overview
The `provider-metrics` capability handles credential discovery, authentication validity tracking, quota metrics calculation (5-hour rolling and 7-day weekly limits), and upstream status monitoring for AI development providers (Claude Code and Google Antigravity). It includes proactive error detection (401/403/expired credentials) and automatic recovery upon token refresh.

---

## Requirements

### REQ-PROV-001: Credential Ingestion and Path Resolution
The provider metrics engine MUST discover and load credentials from configured filesystem locations or environment variables:
1. Claude Code: `~/.claude` (and Claude Code session files `~/.claude.json`, `~/.claude/` session state, or `CLAUDE_CONFIG_DIR`).
2. Google Antigravity: `~/.gemini` (credentials file `~/.gemini/credentials.json`, `~/.gemini/` session storage, or `GEMINI_CONFIG_DIR`).

#### Scenario: Custom mount path resolution
- **Given** the service runs inside a Docker container with environment variable `CLAUDE_CONFIG_DIR=/root/.claude` and `GEMINI_CONFIG_DIR=/root/.gemini`
- **When** the provider metrics engine initializes
- **Then** the engine MUST resolve credential files from the specified environment directory paths.

#### Scenario: Default user home directory fallback
- **Given** no override environment variables are set
- **When** the engine attempts to read provider credentials
- **Then** the engine MUST look for files relative to the user's home directory (`$HOME/.claude` and `$HOME/.gemini`).

---

### REQ-PROV-002: Authentication Validity and Re-Login Detection
The provider metrics engine MUST validate credentials and set `auth_valid` and `re_login_required` flags accordingly:
1. If a credential file is missing or unreadable -> `auth_valid = false`, `re_login_required = true`.
2. If a credential file is empty or malformed JSON -> `auth_valid = false`, `re_login_required = true`.
3. If an authenticated API call to the provider returns HTTP 401 Unauthorized or HTTP 403 Forbidden -> `auth_valid = false`, `re_login_required = true`.
4. If an authenticated API call succeeds -> `auth_valid = true`, `re_login_required = false`.

#### Scenario: Missing credential file
- **Given** no credential file exists at `~/.claude`
- **When** the metrics collection runs for provider `"claude"`
- **Then** the provider record MUST return `auth_valid: false`
- **And** `re_login_required` MUST be `true`
- **And** the metrics values MUST be safely populated with zero values without crashing.

#### Scenario: Token revoked or expired (401 Unauthorized)
- **Given** a credential file exists with an expired token
- **When** the engine queries the provider API and receives HTTP 401 Unauthorized
- **Then** the engine MUST catch the 401 error
- **And** set `auth_valid: false` and `re_login_required: true` for that provider.

---

### REQ-PROV-003: Auto-Recovery on Credential Update
The provider metrics engine MUST automatically recover authentication validity when credential files are rewritten after the user re-logs in via the CLI (`claude` or `agy login`).

#### Scenario: Seamless recovery after CLI authentication
- **Given** provider `"claude"` currently has `re_login_required: true`
- **When** the user executes `claude` in their terminal and the `~/.claude` session file is updated with a valid token
- **Then** on the subsequent polling cycle, the engine MUST re-read the updated credential file
- **And** verify the token with the upstream API
- **And** set `auth_valid: true` and `re_login_required: false`.

---

### REQ-PROV-004: Upstream Status Scraping & Component Filtering
The engine MUST monitor the operational status of AI providers and map them into three standardized states: `"operational"`, `"degraded"`, or `"outage"`.

1. **Claude Code Status**:
   - The engine MUST query `https://status.claude.com/api/v2/summary.json`.
   - The engine MUST specifically filter the `components` array for component ID `yyzkbfz2thpt` (representing "Claude Code").
   - If the component status is `"operational"`, the provider status SHALL be `"operational"`.
   - If the component status is `"degraded_performance"` or `"under_maintenance"`, the provider status SHALL be `"degraded"`.
   - If the component status is `"major_outage"` or `"partial_outage"`, the provider status SHALL be `"outage"`.

2. **Google Antigravity Status**:
   - The engine MUST check Google AI Studio / Google Cloud service health indicators.
   - Map operational, degraded, or outage states accordingly.

#### Scenario: Specific Claude Code component degradation
- **Given** Anthropic's general web UI has an outage, but component `yyzkbfz2thpt` ("Claude Code") is reporting `"operational"`
- **When** the status polling worker executes
- **Then** the provider status for Claude MUST report `"operational"`.

#### Scenario: Network failure during status scrape
- **Given** the upstream status endpoint is temporarily unreachable due to network timeout
- **When** the status poll occurs
- **Then** the engine MUST NOT crash
- **And** the engine MUST retain the last known valid status with a logged warning.

---

### REQ-PROV-005: Quota Metrics Calculation & Window Formatting
The engine MUST compute rolling window usage and human-readable reset timestamps:
1. **5-Hour Quota (`quota_5h`)**:
   - Used units/tokens, total limit, and utilization percentage (`used / limit * 100`).
   - Human-readable reset time formatted as `HH:MM` (e.g. `"15:30"`).
   - Unix epoch `reset_timestamp`.
2. **Weekly Quota (`quota_weekly`)**:
   - Used units/tokens, total limit, and utilization percentage.
   - Human-readable reset time formatted with localized day abbreviation and time (e.g. `"Dom 00:00"` or `"Sun 00:00"`).
   - Unix epoch `reset_timestamp`.

#### Scenario: Quota percentage calculation
- **Given** a 5-hour limit of 100.0 units and current usage of 42.5 units
- **When** metrics are computed
- **Then** `percentage` MUST be `42.5`
- **And** `reset_time` MUST be a non-empty string in `HH:MM` format.
