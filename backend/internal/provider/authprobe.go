package provider

import (
	"context"
	"encoding/json"
	"io"
	"net/http"
	"time"

	"github.com/esp32-ai-monitor/backend/internal/model"
)

const (
	probeTimeout    = 5 * time.Second
	probeTTL        = 5 * time.Minute
	probeBackoffMin = 30 * time.Second
	probeBackoffMax = 10 * time.Minute
	probeBodyLimit  = 64 * 1024 // 64 KiB

	// claudeProbeURL is both the auth probe and the only usage source a Claude
	// Code OAuth token can reach: /api/oauth/usage is not a documented public
	// endpoint and may change or disappear without notice.
	claudeProbeURL  = "https://api.anthropic.com/api/oauth/usage"
	claudeOAuthBeta = "oauth-2025-04-20"
)

// sharedProbeClient is the single reused HTTP client for all auth probes,
// keeping connection/RSS overhead flat regardless of provider count.
var sharedProbeClient = &http.Client{Timeout: probeTimeout}

// ProbeOutcome classifies a single auth-probe attempt.
type ProbeOutcome int

const (
	// ProbeOutcomeValid is a definitive 200: the token works right now.
	ProbeOutcomeValid ProbeOutcome = iota
	// ProbeOutcomeExpired is a definitive "no longer valid" answer (401 for
	// Claude).
	ProbeOutcomeExpired
	// ProbeOutcomeIndeterminate is a definitive HTTP answer that proves
	// neither validity nor expiry (403, or any other unmapped status).
	ProbeOutcomeIndeterminate
	// ProbeOutcomeNetworkFail covers timeout/DNS/TLS/refused/429/5xx: the
	// provider never gave a real answer, so the caller MUST fall back to the
	// local on-disk expiry check rather than guessing.
	ProbeOutcomeNetworkFail
)

// ProbeFunc performs one auth probe and reports its outcome plus whatever
// quota its response body carried (the zero value when the provider exposes
// none, or when no usable response was received).
type ProbeFunc func(ctx context.Context, client *http.Client, accessToken string) (ProbeOutcome, ProbeQuota)

// ProbeClaudeAuthAt is probeClaudeAuth with an injectable URL, exported only
// so tests can point it at an httptest server; production code always calls
// it with claudeProbeURL via probeClaudeAuth.
func ProbeClaudeAuthAt(ctx context.Context, client *http.Client, url, accessToken string) (ProbeOutcome, ProbeQuota) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, url, nil)
	if err != nil {
		return ProbeOutcomeNetworkFail, ProbeQuota{}
	}
	req.Header.Set("Authorization", "Bearer "+accessToken)
	req.Header.Set("anthropic-beta", claudeOAuthBeta)

	resp, err := client.Do(req)
	if err != nil {
		return ProbeOutcomeNetworkFail, ProbeQuota{}
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(io.LimitReader(resp.Body, probeBodyLimit))

	switch {
	case resp.StatusCode == http.StatusOK:
		// A truncated or unparseable body never downgrades a token the 200
		// already proved valid: only the quota goes unavailable.
		return ProbeOutcomeValid, ParseClaudeQuota(body)
	case resp.StatusCode == http.StatusUnauthorized:
		return ProbeOutcomeExpired, ProbeQuota{}
	case resp.StatusCode == http.StatusForbidden:
		return ProbeOutcomeIndeterminate, ProbeQuota{}
	case resp.StatusCode == http.StatusTooManyRequests, resp.StatusCode >= 500:
		return ProbeOutcomeNetworkFail, ProbeQuota{}
	default:
		return ProbeOutcomeIndeterminate, ProbeQuota{}
	}
}

func probeClaudeAuth(ctx context.Context, client *http.Client, accessToken string) (ProbeOutcome, ProbeQuota) {
	return ProbeClaudeAuthAt(ctx, client, claudeProbeURL, accessToken)
}

// ProbeQuota is the usage reading recovered from a probe response body, one
// entry per rolling window.
type ProbeQuota struct {
	FiveHour QuotaWindowInfo
	SevenDay QuotaWindowInfo
}

// QuotaWindowInfo is one rolling window as the provider reported it.
// Available is false whenever the window was missing or unparseable, so the
// caller never fabricates a percentage out of a zero value.
type QuotaWindowInfo struct {
	Available  bool
	Percentage float64
	ResetsAt   time.Time
}

// usageResponse mirrors the /api/oauth/usage payload. The windows are
// pointers so an absent object stays distinguishable from a 0% one.
type usageResponse struct {
	FiveHour *usageWindow `json:"five_hour"`
	SevenDay *usageWindow `json:"seven_day"`
}

type usageWindow struct {
	Utilization float64 `json:"utilization"`
	ResetsAt    string  `json:"resets_at"`
}

// ParseClaudeQuota reads the /api/oauth/usage body. Only the utilization
// percentage is a real reading there: subscription plans report
// limit_dollars/used_dollars as null, so token counts stay unknown.
func ParseClaudeQuota(body []byte) ProbeQuota {
	var resp usageResponse
	if err := json.Unmarshal(body, &resp); err != nil {
		return ProbeQuota{}
	}
	return ProbeQuota{
		FiveHour: quotaWindowInfo(resp.FiveHour),
		SevenDay: quotaWindowInfo(resp.SevenDay),
	}
}

func quotaWindowInfo(w *usageWindow) QuotaWindowInfo {
	if w == nil {
		return QuotaWindowInfo{Available: false}
	}
	// An unparseable resets_at only costs the countdown, not the reading.
	t, _ := time.Parse(time.RFC3339, w.ResetsAt)
	return QuotaWindowInfo{Available: true, Percentage: w.Utilization, ResetsAt: t}
}

// AuthProber owns the probe cadence (TTL + backoff + mtime early re-probe)
// and classification for a single provider engine. It is only ever accessed
// from the single TokenWatcher polling goroutine, so it needs no internal
// locking.
type AuthProber struct {
	client *http.Client
	probe  ProbeFunc

	lastProbeAt time.Time
	lastMtime   time.Time
	lastQuota   ProbeQuota

	verdict     model.AuthState
	stale       bool
	checkedAt   int64
	consecFails int
}

// NewAuthProber builds a prober using the given client and probe function.
// Passing nil for client uses the shared package-level HTTP client.
func NewAuthProber(client *http.Client, probe ProbeFunc) *AuthProber {
	if client == nil {
		client = sharedProbeClient
	}
	return &AuthProber{client: client, probe: probe}
}

// LastQuota returns the quota parsed by the most recent actual probe (the
// zero value if no probe has run yet, or if creds are absent/unreadable).
func (a *AuthProber) LastQuota() ProbeQuota {
	return a.lastQuota
}

// Classify returns the current (state, stale, auth_checked_at) verdict for
// cred, deciding internally whether a fresh network probe is warranted this
// call, and applying the classification rules below.
func (a *AuthProber) Classify(ctx context.Context, cred CredResult, credMtime, now time.Time) (model.AuthState, bool, int64) {
	switch cred.State {
	case CredAbsent:
		a.resetCache()
		return model.AuthStateExpired, false, 0
	case CredUnreadable:
		a.resetCache()
		return model.AuthStateUnknown, true, 0
	}

	// On-disk expiry is authoritative and cannot be overridden by a probe,
	// so a confirmed-past expiry skips the network call.
	if !cred.ExpiresAt.IsZero() && !now.Before(cred.ExpiresAt) {
		a.resetCache()
		return model.AuthStateExpired, false, 0
	}

	if a.shouldProbe(now, credMtime) {
		outcome, quota := a.probe(ctx, a.client, cred.AccessToken)
		a.lastQuota = quota
		a.lastProbeAt = now
		a.lastMtime = credMtime

		switch outcome {
		case ProbeOutcomeValid:
			a.verdict, a.stale, a.consecFails = model.AuthStateValid, false, 0
			a.checkedAt = now.Unix()
		case ProbeOutcomeExpired:
			a.verdict, a.stale, a.consecFails = model.AuthStateExpired, false, 0
			a.checkedAt = now.Unix()
		case ProbeOutcomeIndeterminate:
			a.verdict, a.stale, a.consecFails = model.AuthStateUnknown, true, 0
			a.checkedAt = now.Unix()
		case ProbeOutcomeNetworkFail:
			// Never guess "unknown" here: local expiry already proved
			// not-yet-expired, so the verdict is valid, just stale.
			// auth_checked_at is intentionally left unchanged.
			a.verdict, a.stale = model.AuthStateValid, true
			a.consecFails++
		}
	}

	return a.verdict, a.stale, a.checkedAt
}

func (a *AuthProber) resetCache() {
	a.lastProbeAt = time.Time{}
	a.lastMtime = time.Time{}
	a.lastQuota = ProbeQuota{}
	a.verdict = ""
	a.stale = false
	a.checkedAt = 0
	a.consecFails = 0
}

func (a *AuthProber) shouldProbe(now, credMtime time.Time) bool {
	if a.lastProbeAt.IsZero() {
		return true
	}
	if !credMtime.IsZero() && credMtime.After(a.lastMtime) {
		return true
	}
	elapsed := now.Sub(a.lastProbeAt)
	// A cached window past its own resets_at describes a window that no longer
	// exists upstream, so waiting out the flat TTL just serves a dead reading.
	// Re-probe at the backoff floor instead. Skipped while probes are failing,
	// where the backoff owns the cadence.
	if a.consecFails == 0 && a.quotaWindowExpired(now) && elapsed >= probeBackoffMin {
		return true
	}
	return elapsed >= a.nextInterval()
}

// quotaWindowExpired reports whether any cached window's resets_at moment has
// already passed.
func (a *AuthProber) quotaWindowExpired(now time.Time) bool {
	for _, w := range []QuotaWindowInfo{a.lastQuota.FiveHour, a.lastQuota.SevenDay} {
		if w.Available && !w.ResetsAt.IsZero() && now.After(w.ResetsAt) {
			return true
		}
	}
	return false
}

func (a *AuthProber) nextInterval() time.Duration {
	if a.consecFails <= 0 {
		return probeTTL
	}
	d := probeBackoffMin
	for i := 1; i < a.consecFails; i++ {
		d *= 2
		if d >= probeBackoffMax {
			return probeBackoffMax
		}
	}
	if d > probeBackoffMax {
		d = probeBackoffMax
	}
	return d
}
