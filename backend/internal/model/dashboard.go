package model

type ProviderStatus string

const (
	StatusOperational ProviderStatus = "operational"
	StatusDegraded    ProviderStatus = "degraded"
	StatusOutage      ProviderStatus = "outage"
)

type QuotaWindow struct {
	Used           float64 `json:"used"`
	Limit          float64 `json:"limit"`
	Percentage     float64 `json:"percentage"`
	ResetTime      string  `json:"reset_time"`
	ResetTimestamp int64   `json:"reset_timestamp"`
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
	Metrics         ProviderMetrics `json:"metrics"`
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
