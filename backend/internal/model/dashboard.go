package model

type ProviderStatus string

const (
	StatusOperational ProviderStatus = "operational"
	StatusDegraded    ProviderStatus = "degraded"
	StatusOutage      ProviderStatus = "outage"
)

// AuthState is the tri-state authoritative auth-validity signal.
type AuthState string

const (
	AuthStateValid   AuthState = "valid"
	AuthStateExpired AuthState = "expired"
	AuthStateUnknown AuthState = "unknown"
)

type QuotaWindow struct {
	Used           float64 `json:"used"`
	Limit          float64 `json:"limit"`
	Percentage     float64 `json:"percentage"`
	ResetTime      string  `json:"reset_time"`
	ResetTimestamp int64   `json:"reset_timestamp"`
	// Available is false when no live usage-data source exists; when false,
	// Used/Percentage MUST NOT be read as a real reading.
	Available bool `json:"available"`
	// ResetInSeconds is computed by the backend at payload-build time as the
	// countdown seed for the clockless firmware. Ignored by consumers when
	// Available is false.
	ResetInSeconds int64 `json:"reset_in_seconds"`
}

type ProviderMetrics struct {
	Quota5h     QuotaWindow `json:"quota_5h"`
	QuotaWeekly QuotaWindow `json:"quota_weekly"`
}

type Provider struct {
	ID              string          `json:"id"`
	Name            string          `json:"name"`
	Status          ProviderStatus  `json:"status"`
	AuthValid       bool            `json:"auth_valid"`
	ReLoginRequired bool            `json:"re_login_required"`
	// AuthState is the authoritative tri-state auth signal: valid | expired | unknown.
	AuthState AuthState `json:"auth_state"`
	// AuthCheckedAt is the unix timestamp of the last successful auth probe; 0 = never probed.
	AuthCheckedAt int64 `json:"auth_checked_at"`
	// Stale is true when some part of this report is last-known rather than
	// live: auth validity degraded to a local-expiry check after a network
	// failure, and/or the upstream status feed could not be fetched/parsed.
	Stale   bool            `json:"stale"`
	Metrics ProviderMetrics `json:"metrics"`
}

// ApplyAuthState sets AuthState and derives the legacy AuthValid/ReLoginRequired
// fields from it, so the three fields can never disagree with each other.
func (p *Provider) ApplyAuthState(state AuthState) {
	p.AuthState = state
	p.AuthValid = state == AuthStateValid
	p.ReLoginRequired = state == AuthStateExpired
}

type SystemMetrics struct {
	UptimeSeconds int64   `json:"uptime_seconds"`
	MemoryMB      float64 `json:"memory_mb"`
}

type DashboardResponse struct {
	Timestamp int64         `json:"timestamp"`
	Providers []Provider    `json:"providers"`
	System    SystemMetrics `json:"system"`
}
