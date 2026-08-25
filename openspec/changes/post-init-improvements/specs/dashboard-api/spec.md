# Delta for dashboard-api

## MODIFIED Requirements

### Requirement: JSON Payload Schema
The `GET /api/dashboard` response MUST adhere to the existing schema (unchanged fields, unchanged types) PLUS the following additive-only fields:
1. `providers[].stale` (boolean, default `false`): mirrors provider-metrics `stale` — `true` when any part of the provider report is last-known rather than live: auth validity degraded to a local-expiry check after a network failure, and/or the upstream status feed could not be fetched or parsed.
2. `providers[].metrics.quota_5h.available` and `providers[].metrics.quota_weekly.available` (boolean, default `true`): `false` when no live usage-data source exists; when `false`, `used`/`percentage` MUST NOT be presented as a real reading.
3. `providers[].metrics.quota_5h.reset_timestamp` / `quota_weekly.reset_timestamp` (already present): confirmed as Unix epoch seconds UTC, retained for wall-clock consumers.
4. `providers[].metrics.quota_5h.reset_in_seconds` and `quota_weekly.reset_in_seconds` (integer, seconds remaining): computed by the backend at payload-build time as the countdown source for firmware, which has no NTP/wall clock. Absent or MUST be ignored by consumers when the corresponding `available` is `false`.
5. `providers[].auth_state` (string enum: `valid` | `expired` | `unknown`): the authoritative auth-status signal. Legacy `auth_valid` and `re_login_required` are retained as derived fields for backward compatibility: `auth_valid` MUST be `true` only when `auth_state == "valid"`; `re_login_required` MUST be `true` only when `auth_state == "expired"`.

No existing field MUST be removed, renamed, or change type; all additions MUST be optional/additive for existing consumers.
(Previously: schema had no availability signal — unavailable usage and real `0%` usage were indistinguishable — no explicit staleness signal for degraded auth checks, no backend-computed countdown seconds, and no tri-state auth signal distinguishing "expired" from "unknown/transient failure".)

#### Scenario: Provider data structure validation (unchanged fields)
- **Given** a valid dashboard response
- **When** evaluating a provider object
- **Then** it MUST still include `id`, `name`, `status`, `auth_valid`, `re_login_required`, and `metrics.{quota_5h,quota_weekly}` with `used`, `limit`, `percentage`, `reset_time` unchanged in type/meaning.

#### Scenario: Unavailable quota is schema-visible
- **Given** a provider has no live quota source
- **When** `/api/dashboard` is requested
- **Then** `quota_5h.available` MUST be `false`, distinguishing it from a real `0%` reading
- **And** `quota_5h.reset_in_seconds` MUST be absent or MUST be ignored by consumers.

#### Scenario: Degraded auth is schema-visible
- **Given** a provider's auth check fell back to local-expiry after a network failure
- **When** `/api/dashboard` is requested
- **Then** `providers[].stale` MUST be `true` while `auth_valid` MAY remain `true`.

#### Scenario: Additive-only compatibility
- **Given** a client that only reads the pre-change field set
- **When** it receives the new payload
- **Then** every pre-change field MUST still be present with its original type and meaning, unaffected by the new fields.

#### Scenario: Backend computes remaining seconds for the firmware countdown
- **Given** the firmware has no NTP/wall clock and cannot compute `reset_timestamp - current_time` on-device
- **When** the backend builds the `/api/dashboard` payload and `quota_5h.available == true`
- **Then** `quota_5h.reset_in_seconds` MUST equal the correct number of seconds remaining until reset, computed by the backend at payload-build time
- **And** the firmware MUST seed its countdown from that value and tick it locally (millis-based) between polls, resyncing `reset_in_seconds` on each poll.

#### Scenario: Auth state distinguishes expired from unknown
- **Given** a provider's authenticated check could not be completed (e.g. transient network failure) rather than confirmed expired
- **When** `/api/dashboard` is requested
- **Then** `auth_state` MUST be `"unknown"`, `re_login_required` MUST be `false`, and `auth_valid` MUST be `false`
- **And** this MUST be distinguishable from `auth_state == "expired"`, where `re_login_required` MUST be `true`.
