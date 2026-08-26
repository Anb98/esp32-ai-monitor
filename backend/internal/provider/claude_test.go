package provider_test

import (
	"context"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"testing"
	"time"

	"github.com/esp32-ai-monitor/backend/internal/model"
	"github.com/esp32-ai-monitor/backend/internal/provider"
)

func writeClaudeCredentials(t *testing.T, dir string, accessToken string, expiresAt time.Time) {
	t.Helper()
	content := `{"claudeAiOauth":{"accessToken":"` + accessToken + `","refreshToken":"r","expiresAt":` +
		strconv.FormatInt(expiresAt.UnixMilli(), 10) + `}}`
	if err := os.WriteFile(filepath.Join(dir, ".credentials.json"), []byte(content), 0600); err != nil {
		t.Fatalf("failed to write claude credentials fixture: %v", err)
	}
}

func alwaysValidProbe(ctx context.Context, client *http.Client, token string) (provider.ProbeOutcome, provider.ProbeQuota) {
	return provider.ProbeOutcomeValid, provider.ProbeQuota{}
}

func newTestClaudeEngine(configDir, home string, scraper *provider.StatusScraper, probe provider.ProbeFunc) *provider.ClaudeEngine {
	return provider.NewClaudeEngineWithProber(configDir, home, scraper, provider.NewAuthProber(http.DefaultClient, probe))
}

func TestClaudeEngine_ValidCredentials_ProbeConfirmsValid(t *testing.T) {
	tempDir := t.TempDir()
	writeClaudeCredentials(t, tempDir, "sk-ant-oat01-valid", time.Now().Add(24*time.Hour))

	engine := newTestClaudeEngine(tempDir, t.TempDir(), provider.NewStatusScraper(), alwaysValidProbe)

	if engine.ID() != "claude" {
		t.Errorf("expected ID 'claude', got %s", engine.ID())
	}
	if engine.Name() != "Claude Code" {
		t.Errorf("expected Name 'Claude Code', got %s", engine.Name())
	}

	p, err := engine.FetchMetrics(context.Background())
	if err != nil {
		t.Fatalf("unexpected error fetching metrics: %v", err)
	}

	if p.AuthState != model.AuthStateValid {
		t.Errorf("expected AuthState valid, got %v", p.AuthState)
	}
	if !p.AuthValid || p.ReLoginRequired {
		t.Errorf("expected AuthValid=true, ReLoginRequired=false, got AuthValid=%v ReLoginRequired=%v", p.AuthValid, p.ReLoginRequired)
	}
	if p.AuthCheckedAt == 0 {
		t.Error("expected AuthCheckedAt to be set after a probe ran")
	}
}

func TestClaudeEngine_MissingCredentials_IsExpiredNoCrash(t *testing.T) {
	tempDir := t.TempDir() // no .credentials.json

	engine := newTestClaudeEngine(tempDir, t.TempDir(), provider.NewStatusScraper(), alwaysValidProbe)

	p, err := engine.FetchMetrics(context.Background())
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}

	if p.AuthState != model.AuthStateExpired {
		t.Errorf("expected AuthState expired for missing credentials, got %v", p.AuthState)
	}
	if p.AuthValid {
		t.Error("expected AuthValid=false for missing credentials")
	}
	if !p.ReLoginRequired {
		t.Error("expected ReLoginRequired=true for missing credentials")
	}
	if p.Metrics.Quota5h.Available {
		t.Error("expected quota_5h.available=false with no live usage source")
	}
}

func TestClaudeEngine_UnknownLayout_IsUnknownNeverFalseReLogin(t *testing.T) {
	tempDir := t.TempDir()
	if err := os.WriteFile(filepath.Join(tempDir, ".credentials.json"), []byte(`{"unexpected":"shape"}`), 0600); err != nil {
		t.Fatalf("failed to write fixture: %v", err)
	}

	engine := newTestClaudeEngine(tempDir, t.TempDir(), provider.NewStatusScraper(), alwaysValidProbe)

	p, err := engine.FetchMetrics(context.Background())
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}

	if p.AuthState != model.AuthStateUnknown {
		t.Errorf("expected AuthState unknown for an unrecognized credential layout, got %v", p.AuthState)
	}
	if p.ReLoginRequired {
		t.Error("expected ReLoginRequired=false for unknown auth state (never a false re-login)")
	}
	if !p.Stale {
		t.Error("expected Stale=true for unknown auth state")
	}
}

func TestClaudeEngine_ExpiredOnDisk_IsExpiredRegardlessOfProbe(t *testing.T) {
	tempDir := t.TempDir()
	writeClaudeCredentials(t, tempDir, "sk-ant-oat01-expired", time.Now().Add(-1*time.Hour))

	// alwaysValidProbe would say "valid" if it were ever called; on-disk
	// expiry must win regardless.
	engine := newTestClaudeEngine(tempDir, t.TempDir(), provider.NewStatusScraper(), alwaysValidProbe)

	p, err := engine.FetchMetrics(context.Background())
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}

	if p.AuthState != model.AuthStateExpired {
		t.Errorf("expected AuthState expired, got %v", p.AuthState)
	}
	if !p.ReLoginRequired {
		t.Error("expected ReLoginRequired=true for an on-disk expired token")
	}
}

func TestClaudeEngine_AbsentPrimaryButAccountHintInClaudeJSON_MarksStale(t *testing.T) {
	claudeDir := t.TempDir() // no .credentials.json
	home := t.TempDir()
	if err := os.WriteFile(filepath.Join(home, ".claude.json"), []byte(`{"oauthAccount":{"emailAddress":"a@b.com"}}`), 0600); err != nil {
		t.Fatalf("failed to write claude.json fixture: %v", err)
	}

	engine := newTestClaudeEngine(claudeDir, home, provider.NewStatusScraper(), alwaysValidProbe)

	p, err := engine.FetchMetrics(context.Background())
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}

	if p.AuthState != model.AuthStateExpired {
		t.Errorf("expected AuthState still expired (absent is authoritative), got %v", p.AuthState)
	}
	if !p.Stale {
		t.Error("expected Stale=true when $HOME/.claude.json shows account evidence despite a missing primary token file")
	}
}

func TestClaudeEngine_NoQuotaInProbe_IsUnavailableNeverZero(t *testing.T) {
	tempDir := t.TempDir()
	writeClaudeCredentials(t, tempDir, "sk-ant-oat01-valid", time.Now().Add(24*time.Hour))

	engine := newTestClaudeEngine(tempDir, t.TempDir(), provider.NewStatusScraper(), alwaysValidProbe)

	p, err := engine.FetchMetrics(context.Background())
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}

	if p.Metrics.Quota5h.Available {
		t.Error("expected quota_5h.available=false when the probe carried no usage payload")
	}
	if p.Metrics.QuotaWeekly.Available {
		t.Error("expected quota_weekly.available=false when the probe carried no usage payload")
	}
}

func TestClaudeEngine_UsageQuotaPresent_PopulatesBothWindows(t *testing.T) {
	tempDir := t.TempDir()
	writeClaudeCredentials(t, tempDir, "sk-ant-oat01-valid", time.Now().Add(24*time.Hour))

	resetsAt := time.Now().Add(90 * time.Minute)
	probeWithQuota := func(ctx context.Context, client *http.Client, token string) (provider.ProbeOutcome, provider.ProbeQuota) {
		return provider.ProbeOutcomeValid, provider.ProbeQuota{
			FiveHour: provider.QuotaWindowInfo{Available: true, Percentage: 7, ResetsAt: resetsAt},
			SevenDay: provider.QuotaWindowInfo{Available: true, Percentage: 44},
		}
	}

	engine := newTestClaudeEngine(tempDir, t.TempDir(), provider.NewStatusScraper(), probeWithQuota)

	p, err := engine.FetchMetrics(context.Background())
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}

	if !p.Metrics.Quota5h.Available || p.Metrics.Quota5h.Percentage != 7 {
		t.Errorf("expected quota_5h available at 7 percent, got %+v", p.Metrics.Quota5h)
	}
	if !p.Metrics.QuotaWeekly.Available || p.Metrics.QuotaWeekly.Percentage != 44 {
		t.Errorf("expected quota_weekly available at 44 percent, got %+v", p.Metrics.QuotaWeekly)
	}
	// The usage endpoint reports no token counts on subscription plans, so
	// these must stay 0 rather than a derived guess.
	if p.Metrics.Quota5h.Used != 0 || p.Metrics.Quota5h.Limit != 0 {
		t.Errorf("expected no fabricated used/limit counts, got used=%v limit=%v", p.Metrics.Quota5h.Used, p.Metrics.Quota5h.Limit)
	}
	if p.Metrics.Quota5h.ResetTimestamp != resetsAt.Unix() {
		t.Errorf("expected ResetTimestamp=%d, got %d", resetsAt.Unix(), p.Metrics.Quota5h.ResetTimestamp)
	}
	if p.Metrics.Quota5h.ResetInSeconds <= 0 || p.Metrics.Quota5h.ResetInSeconds > 5400 {
		t.Errorf("expected a ~90 minute countdown, got %d", p.Metrics.Quota5h.ResetInSeconds)
	}
	if p.Metrics.Quota5h.ResetTime != resetsAt.Local().Format("3:04 PM") {
		t.Errorf("expected ResetTime=%q, got %q", resetsAt.Local().Format("3:04 PM"), p.Metrics.Quota5h.ResetTime)
	}
	// A window with no resets_at still carries a real percentage.
	if p.Metrics.QuotaWeekly.ResetTimestamp != 0 || p.Metrics.QuotaWeekly.ResetInSeconds != 0 {
		t.Errorf("expected a zero reset for a window without resets_at, got %+v", p.Metrics.QuotaWeekly)
	}
}

