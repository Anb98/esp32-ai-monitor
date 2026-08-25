# Capability: dashboard-api

## Overview
The `dashboard-api` capability defines the lightweight HTTP service running in Go (<10MB RAM) that consolidates AI provider metrics, credentials validity, upstream operational statuses, and local system telemetry. It provides high-performance, in-memory JSON endpoints over the local network to embedded clients (such as the ESP32-S3 AMOLED monitor) and developer tooling.

---

## Requirements

### REQ-DASH-001: Consolidated Dashboard JSON Endpoint
The API server MUST expose a `GET /api/dashboard` endpoint that returns a JSON payload containing timestamped metrics for all configured providers (`claude`, `antigravity`) and system diagnostics.

#### Scenario: Successful dashboard payload retrieval
- **Given** the Go backend service is running and provider metrics have been populated
- **When** an HTTP client sends a `GET /api/dashboard` request
- **Then** the server MUST respond with HTTP status 200 OK
- **And** the `Content-Type` header MUST be `application/json`
- **And** the payload MUST conform to the dashboard JSON schema containing `timestamp`, `providers`, and `system`.

#### Scenario: Provider data structure validation
- **Given** a valid dashboard response
- **When** evaluating each item in the `providers` array
- **Then** each provider object MUST include `id` (string: `"claude"` | `"antigravity"`), `name` (string), `status` (string: `"operational"` | `"degraded"` | `"outage"`), `auth_valid` (boolean), `re_login_required` (boolean), and a `metrics` object containing `quota_5h` and `quota_weekly` with `used`, `limit`, `percentage`, and `reset_time` fields.

---

### REQ-DASH-002: Health Check Endpoint
The API server MUST expose a `GET /healthz` endpoint for liveness and container orchestration probes.

#### Scenario: Health probe succeeds
- **Given** the service process is running and accepting HTTP connections
- **When** a client sends a `GET /healthz` request
- **Then** the server MUST respond with HTTP status 200 OK
- **And** the body MUST be `{"status":"ok"}` with `Content-Type: application/json`.

---

### REQ-DASH-003: In-Memory Caching and Non-Blocking Serving
The API server MUST serve `GET /api/dashboard` requests directly from a thread-safe in-memory cache protected by a Read-Write mutex (`sync.RWMutex`). Incoming HTTP requests SHALL NOT block on external upstream API requests or file I/O operations.

#### Scenario: High concurrency cache serving
- **Given** an active background polling cycle of 30 seconds
- **When** multiple concurrent clients request `GET /api/dashboard` within a single polling window
- **Then** all requests MUST be fulfilled from the in-memory cache in under 10 milliseconds
- **And** no duplicate external upstream network calls SHALL be triggered by incoming client requests.

---

### REQ-DASH-004: Memory Constraint and Footprint
The Go backend process MUST operate within a strict memory budget of less than 10MB of resident set size (RSS) under continuous operation.

#### Scenario: Continuous operation memory budget compliance
- **Given** the Go backend is running in a minimal Alpine or scratch Docker container
- **When** the service executes continuous 30-second polling and responds to periodic dashboard client requests over a 24-hour period
- **Then** the total process memory consumption MUST remain strictly below 10MB RAM.

---

### REQ-DASH-005: JSON Payload Schema
The `GET /api/dashboard` response MUST adhere to the following strict JSON schema:

```json
{
  "timestamp": 1756083867,
  "providers": [
    {
      "id": "claude",
      "name": "Claude Code",
      "status": "operational",
      "auth_valid": true,
      "re_login_required": false,
      "metrics": {
        "quota_5h": {
          "used": 42.5,
          "limit": 100.0,
          "percentage": 42.5,
          "reset_time": "15:30",
          "reset_timestamp": 1756090200
        },
        "quota_weekly": {
          "used": 150.0,
          "limit": 500.0,
          "percentage": 30.0,
          "reset_time": "Dom 00:00",
          "reset_timestamp": 1756684800
        }
      }
    },
    {
      "id": "antigravity",
      "name": "Google Antigravity",
      "status": "operational",
      "auth_valid": true,
      "re_login_required": false,
      "metrics": {
        "quota_5h": {
          "used": 12.0,
          "limit": 100.0,
          "percentage": 12.0,
          "reset_time": "18:00",
          "reset_timestamp": 1756099200
        },
        "quota_weekly": {
          "used": 80.0,
          "limit": 1000.0,
          "percentage": 8.0,
          "reset_time": "Lun 00:00",
          "reset_timestamp": 1756771200
        }
      }
    }
  ],
  "system": {
    "uptime_seconds": 3600,
    "memory_mb": 4.8
  }
}
```
