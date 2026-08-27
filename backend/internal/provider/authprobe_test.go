package provider_test

import (
	"context"
	"net/http"
	"net/http/httptest"
	"os"
	"testing"
	"time"

	"github.com/esp32-ai-monitor/backend/internal/model"
	"github.com/esp32-ai-monitor/backend/internal/provider"
)

// --- concrete probe status-code mapping (httptest, real HTTP) ---

func TestProbeClaudeAuth_StatusMapping(t *testing.T) {
	tests := []struct {
		name    string
		status  int
		wantErr bool // true => probe reports a "keep local verdict" network-class failure
	}{
		{"200 is not an error", http.StatusOK, false},
		{"401 is not a network failure", http.StatusUnauthorized, false},
		{"403 is not a network failure", http.StatusForbidden, false},
		{"429 is a network-class failure", http.StatusTooManyRequests, true},
		{"500 is a network-class failure", http.StatusInternalServerError, true},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				if r.Method != http.MethodGet {
					t.Errorf("expected GET, got %s", r.Method)
				}
				if r.Header.Get("Authorization") != "Bearer test-token" {
					t.Errorf("expected Bearer auth header, got %q", r.Header.Get("Authorization"))
				}
				if r.Header.Get("anthropic-beta") != "oauth-2025-04-20" {
					t.Errorf("expected the oauth beta header, got %q", r.Header.Get("anthropic-beta"))
				}
				w.WriteHeader(tt.status)
				_, _ = w.Write([]byte(`{}`))
			}))
			defer server.Close()

			outcome, _ := provider.ProbeClaudeAuthAt(context.Background(), server.Client(), server.URL, "test-token")
			isNetFail := outcome == provider.ProbeOutcomeNetworkFail
			if isNetFail != tt.wantErr {
				t.Errorf("status %d: expected networkFail=%v, got outcome=%v", tt.status, tt.wantErr, outcome)
			}
		})
	}
}

func TestProbeClaudeAuth_200IsValid(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
	}))
	defer server.Close()

	outcome, _ := provider.ProbeClaudeAuthAt(context.Background(), server.Client(), server.URL, "tok")
	if outcome != provider.ProbeOutcomeValid {
		t.Errorf("expected ProbeOutcomeValid, got %v", outcome)
	}
}

func TestProbeClaudeAuth_401IsExpired(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusUnauthorized)
	}))
	defer server.Close()

	outcome, quota := provider.ProbeClaudeAuthAt(context.Background(), server.Client(), server.URL, "tok")
	if outcome != provider.ProbeOutcomeExpired {
		t.Errorf("expected ProbeOutcomeExpired, got %v", outcome)
	}
	if quota.FiveHour.Available || quota.SevenDay.Available {
		t.Error("expected no quota reading from a 401 response")
	}
}

func TestProbeClaudeAuth_403IsIndeterminate(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusForbidden)
	}))
	defer server.Close()

	outcome, _ := provider.ProbeClaudeAuthAt(context.Background(), server.Client(), server.URL, "tok")
	if outcome != provider.ProbeOutcomeIndeterminate {
		t.Errorf("expected ProbeOutcomeIndeterminate, got %v", outcome)
	}
}

func TestProbeClaudeAuth_ConnectionRefusedIsNetworkFail(t *testing.T) {
	// Port 0 server closed immediately -> connection refused.
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {}))
	url := server.URL
	server.Close() // now guaranteed refused

	outcome, quota := provider.ProbeClaudeAuthAt(context.Background(), http.DefaultClient, url, "tok")
	if outcome != provider.ProbeOutcomeNetworkFail {
		t.Errorf("expected ProbeOutcomeNetworkFail on connection refused, got %v", outcome)
	}
	if quota.FiveHour.Available || quota.SevenDay.Available {
		t.Error("expected no quota reading on a pure network failure")
	}
}

// --- quota parsed from the /api/oauth/usage body ---

func serveUsage(t *testing.T, body []byte) *httptest.Server {
	t.Helper()
	return httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write(body)
	}))
}

func TestProbeClaudeAuth_200ParsesBothWindows(t *testing.T) {
	body, err := os.ReadFile("testdata/claude/oauth_usage.json")
	if err != nil {
		t.Fatalf("failed to read usage fixture: %v", err)
	}
	server := serveUsage(t, body)
	defer server.Close()

	outcome, quota := provider.ProbeClaudeAuthAt(context.Background(), server.Client(), server.URL, "tok")
	if outcome != provider.ProbeOutcomeValid {
		t.Fatalf("expected ProbeOutcomeValid, got %v", outcome)
	}
	if !quota.FiveHour.Available || quota.FiveHour.Percentage != 7.0 {
		t.Errorf("expected five_hour available at 7 percent, got %+v", quota.FiveHour)
	}
	if !quota.SevenDay.Available || quota.SevenDay.Percentage != 44.0 {
		t.Errorf("expected seven_day available at 44 percent, got %+v", quota.SevenDay)
	}
	wantReset := time.Date(2026, 8, 25, 20, 0, 0, 0, time.UTC)
	if quota.FiveHour.ResetsAt.Unix() != wantReset.Unix() {
		t.Errorf("expected five_hour resets_at %v, got %v", wantReset, quota.FiveHour.ResetsAt)
	}
}

func TestProbeClaudeAuth_200CorruptBodyStaysValidWithoutQuota(t *testing.T) {
	server := serveUsage(t, []byte(`{"five_hour":`))
	defer server.Close()

	outcome, quota := provider.ProbeClaudeAuthAt(context.Background(), server.Client(), server.URL, "tok")
	if outcome != provider.ProbeOutcomeValid {
		t.Errorf("expected an unparseable body to leave the proven token valid, got %v", outcome)
	}
	if quota.FiveHour.Available || quota.SevenDay.Available {
		t.Error("expected no fabricated quota from an unparseable body")
	}
}

func TestParseClaudeQuota_MissingWindowIsUnavailable(t *testing.T) {
	quota := provider.ParseClaudeQuota([]byte(`{"five_hour":{"utilization":12.5,"resets_at":"2026-08-25T20:00:00+00:00"}}`))
	if !quota.FiveHour.Available || quota.FiveHour.Percentage != 12.5 {
		t.Errorf("expected the present window to be read, got %+v", quota.FiveHour)
	}
	if quota.SevenDay.Available {
		t.Error("expected an absent seven_day window to stay unavailable")
	}
}

// --- AuthProber classification + cadence, using a stub probe (no HTTP) ---

func stubProbe(outcomes ...provider.ProbeOutcome) (provider.ProbeFunc, *int) {
	calls := 0
	return func(ctx context.Context, client *http.Client, accessToken string) (provider.ProbeOutcome, provider.ProbeQuota) {
		idx := calls
		if idx >= len(outcomes) {
			idx = len(outcomes) - 1
		}
		calls++
		return outcomes[idx], provider.ProbeQuota{}
	}, &calls
}

func TestAuthProber_CredAbsent_IsExpiredNeverProbed(t *testing.T) {
	probe, calls := stubProbe(provider.ProbeOutcomeValid)
	prober := provider.NewAuthProber(http.DefaultClient, probe)

	state, stale, checkedAt := prober.Classify(context.Background(), provider.CredResult{State: provider.CredAbsent}, time.Time{}, time.Now())

	if state != model.AuthStateExpired {
		t.Errorf("expected AuthStateExpired, got %v", state)
	}
	if stale {
		t.Error("expected stale=false for absent credentials (not a network degradation)")
	}
	if checkedAt != 0 {
		t.Errorf("expected checkedAt=0 (never probed), got %d", checkedAt)
	}
	if *calls != 0 {
		t.Errorf("expected probe never invoked for absent credentials, called %d times", *calls)
	}
}

func TestAuthProber_CredUnreadable_IsUnknownNeverExpired(t *testing.T) {
	probe, calls := stubProbe(provider.ProbeOutcomeValid)
	prober := provider.NewAuthProber(http.DefaultClient, probe)

	state, stale, checkedAt := prober.Classify(context.Background(), provider.CredResult{State: provider.CredUnreadable}, time.Time{}, time.Now())

	if state != model.AuthStateUnknown {
		t.Errorf("expected AuthStateUnknown for unreadable creds, got %v", state)
	}
	if !stale {
		t.Error("expected stale=true for unreadable creds")
	}
	if checkedAt != 0 {
		t.Errorf("expected checkedAt=0, got %d", checkedAt)
	}
	if *calls != 0 {
		t.Errorf("expected probe never invoked for unreadable credentials, called %d times", *calls)
	}
}

func TestAuthProber_OnDiskExpiryPast_OverridesProbeResult(t *testing.T) {
	probe, calls := stubProbe(provider.ProbeOutcomeValid) // would say "valid" if ever called
	prober := provider.NewAuthProber(http.DefaultClient, probe)

	now := time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)
	cred := provider.CredResult{State: provider.CredFound, AccessToken: "tok", ExpiresAt: now.Add(-1 * time.Hour)}

	state, stale, _ := prober.Classify(context.Background(), cred, time.Time{}, now)

	if state != model.AuthStateExpired {
		t.Errorf("expected on-disk expiry to be authoritative (expired), got %v", state)
	}
	if stale {
		t.Error("expected stale=false for a confirmed local expiry")
	}
	if *calls != 0 {
		t.Errorf("expected the probe to be skipped when local expiry already proves expiry, called %d times", *calls)
	}
}

func TestAuthProber_ProbeValid(t *testing.T) {
	probe, _ := stubProbe(provider.ProbeOutcomeValid)
	prober := provider.NewAuthProber(http.DefaultClient, probe)

	now := time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)
	cred := provider.CredResult{State: provider.CredFound, AccessToken: "tok", ExpiresAt: now.Add(1 * time.Hour)}

	state, stale, checkedAt := prober.Classify(context.Background(), cred, time.Time{}, now)
	if state != model.AuthStateValid || stale {
		t.Errorf("expected valid/not-stale, got state=%v stale=%v", state, stale)
	}
	if checkedAt != now.Unix() {
		t.Errorf("expected checkedAt=%d, got %d", now.Unix(), checkedAt)
	}
}

func TestAuthProber_ProbeExpired(t *testing.T) {
	probe, _ := stubProbe(provider.ProbeOutcomeExpired)
	prober := provider.NewAuthProber(http.DefaultClient, probe)

	now := time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)
	cred := provider.CredResult{State: provider.CredFound, AccessToken: "tok", ExpiresAt: now.Add(1 * time.Hour)}

	state, stale, _ := prober.Classify(context.Background(), cred, time.Time{}, now)
	if state != model.AuthStateExpired || stale {
		t.Errorf("expected expired/not-stale, got state=%v stale=%v", state, stale)
	}
}

func TestAuthProber_ProbeIndeterminate_IsUnknownAndStale(t *testing.T) {
	probe, _ := stubProbe(provider.ProbeOutcomeIndeterminate)
	prober := provider.NewAuthProber(http.DefaultClient, probe)

	now := time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)
	cred := provider.CredResult{State: provider.CredFound, AccessToken: "tok", ExpiresAt: now.Add(1 * time.Hour)}

	state, stale, checkedAt := prober.Classify(context.Background(), cred, time.Time{}, now)
	if state != model.AuthStateUnknown {
		t.Errorf("expected AuthStateUnknown for indeterminate probe, got %v", state)
	}
	if !stale {
		t.Error("expected stale=true for indeterminate probe")
	}
	if checkedAt != now.Unix() {
		t.Error("expected checkedAt updated even for an indeterminate-but-definitive response")
	}
}

func TestAuthProber_NetworkFail_PreservesValidNeverUnknown(t *testing.T) {
	probe, _ := stubProbe(provider.ProbeOutcomeNetworkFail)
	prober := provider.NewAuthProber(http.DefaultClient, probe)

	now := time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)
	cred := provider.CredResult{State: provider.CredFound, AccessToken: "tok", ExpiresAt: now.Add(1 * time.Hour)}

	state, stale, checkedAt := prober.Classify(context.Background(), cred, time.Time{}, now)
	if state != model.AuthStateValid {
		t.Errorf("expected AuthStateValid preserved on network failure (never unknown), got %v", state)
	}
	if !stale {
		t.Error("expected stale=true on network failure")
	}
	if checkedAt != 0 {
		t.Errorf("expected checkedAt unchanged (0, never successfully probed) on network failure, got %d", checkedAt)
	}
}

func TestAuthProber_TTLCacheHit_NoReProbeWithinWindow(t *testing.T) {
	probe, calls := stubProbe(provider.ProbeOutcomeValid, provider.ProbeOutcomeValid)
	prober := provider.NewAuthProber(http.DefaultClient, probe)

	t0 := time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)
	cred := provider.CredResult{State: provider.CredFound, AccessToken: "tok", ExpiresAt: t0.Add(24 * time.Hour)}

	prober.Classify(context.Background(), cred, time.Time{}, t0)
	if *calls != 1 {
		t.Fatalf("expected 1 probe call after first Classify, got %d", *calls)
	}

	// Within the 5-minute TTL: must reuse the cached verdict, no re-probe.
	prober.Classify(context.Background(), cred, time.Time{}, t0.Add(2*time.Minute))
	if *calls != 1 {
		t.Errorf("expected no re-probe within TTL window, got %d calls", *calls)
	}

	// Past the 5-minute TTL: must re-probe.
	prober.Classify(context.Background(), cred, time.Time{}, t0.Add(6*time.Minute))
	if *calls != 2 {
		t.Errorf("expected re-probe after TTL elapses, got %d calls", *calls)
	}
}

func TestAuthProber_MtimeChange_ForcesEarlyReProbe(t *testing.T) {
	probe, calls := stubProbe(provider.ProbeOutcomeValid, provider.ProbeOutcomeValid)
	prober := provider.NewAuthProber(http.DefaultClient, probe)

	t0 := time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)
	cred := provider.CredResult{State: provider.CredFound, AccessToken: "tok", ExpiresAt: t0.Add(24 * time.Hour)}
	mtime0 := t0.Add(-1 * time.Hour)

	prober.Classify(context.Background(), cred, mtime0, t0)
	if *calls != 1 {
		t.Fatalf("expected 1 probe call, got %d", *calls)
	}

	// Well within TTL, but the credential file changed on disk -> re-probe early.
	mtime1 := t0.Add(30 * time.Second)
	prober.Classify(context.Background(), cred, mtime1, t0.Add(1*time.Minute))
	if *calls != 2 {
		t.Errorf("expected an early re-probe on credential mtime change, got %d calls", *calls)
	}
}

func TestAuthProber_BackoffAfterNetworkFail(t *testing.T) {
	probe, calls := stubProbe(provider.ProbeOutcomeNetworkFail, provider.ProbeOutcomeValid, provider.ProbeOutcomeValid)
	prober := provider.NewAuthProber(http.DefaultClient, probe)

	t0 := time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)
	cred := provider.CredResult{State: provider.CredFound, AccessToken: "tok", ExpiresAt: t0.Add(24 * time.Hour)}

	prober.Classify(context.Background(), cred, time.Time{}, t0)
	if *calls != 1 {
		t.Fatalf("expected 1 probe call, got %d", *calls)
	}

	// 20s later: below the 30s minimum backoff -> no re-probe yet.
	prober.Classify(context.Background(), cred, time.Time{}, t0.Add(20*time.Second))
	if *calls != 1 {
		t.Errorf("expected no re-probe before the 30s backoff elapses, got %d calls", *calls)
	}

	// 31s later: past the 30s backoff -> re-probe.
	prober.Classify(context.Background(), cred, time.Time{}, t0.Add(31*time.Second))
	if *calls != 2 {
		t.Errorf("expected re-probe after the 30s backoff elapses, got %d calls", *calls)
	}
}

func TestAuthProber_LastQuota_CapturedFromProbe(t *testing.T) {
	prober := provider.NewAuthProber(http.DefaultClient, func(ctx context.Context, client *http.Client, token string) (provider.ProbeOutcome, provider.ProbeQuota) {
		return provider.ProbeOutcomeValid, provider.ProbeQuota{
			FiveHour: provider.QuotaWindowInfo{Available: true, Percentage: 10},
		}
	})

	now := time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)
	cred := provider.CredResult{State: provider.CredFound, AccessToken: "tok", ExpiresAt: now.Add(1 * time.Hour)}
	prober.Classify(context.Background(), cred, time.Time{}, now)

	if got := prober.LastQuota().FiveHour; !got.Available || got.Percentage != 10 {
		t.Errorf("expected LastQuota to expose the quota parsed by the probe, got %+v", got)
	}
}

func TestAuthProber_WindowPastResetsAt_ForcesEarlyReProbe(t *testing.T) {
	t0 := time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)
	resetsAt := t0.Add(1 * time.Minute)

	calls := 0
	prober := provider.NewAuthProber(http.DefaultClient, func(ctx context.Context, client *http.Client, token string) (provider.ProbeOutcome, provider.ProbeQuota) {
		calls++
		return provider.ProbeOutcomeValid, provider.ProbeQuota{
			FiveHour: provider.QuotaWindowInfo{Available: true, Percentage: 97, ResetsAt: resetsAt},
		}
	})
	cred := provider.CredResult{State: provider.CredFound, AccessToken: "tok", ExpiresAt: t0.Add(24 * time.Hour)}

	prober.Classify(context.Background(), cred, time.Time{}, t0)
	if calls != 1 {
		t.Fatalf("expected 1 probe call, got %d", calls)
	}

	// Window still open: the flat TTL owns the cadence, no re-probe.
	prober.Classify(context.Background(), cred, time.Time{}, t0.Add(45*time.Second))
	if calls != 1 {
		t.Errorf("expected no re-probe while the window is still open, got %d calls", calls)
	}

	// Past resets_at but far inside the 5m TTL: the reset boundary must force
	// the probe instead of leaving a dead window on screen for minutes.
	prober.Classify(context.Background(), cred, time.Time{}, t0.Add(70*time.Second))
	if calls != 2 {
		t.Errorf("expected an early re-probe once resets_at passed, got %d calls", calls)
	}

	// Inside the 30s floor: a still-expired cached window must not hammer.
	prober.Classify(context.Background(), cred, time.Time{}, t0.Add(90*time.Second))
	if calls != 2 {
		t.Errorf("expected the 30s floor to hold off another forced probe, got %d calls", calls)
	}
}
