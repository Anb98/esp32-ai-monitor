package provider_test

import (
	"context"
	"errors"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/esp32-ai-monitor/backend/internal/model"
	"github.com/esp32-ai-monitor/backend/internal/provider"
)

// erroringEngine simulates a provider engine whose FetchMetrics call fails
// (e.g. a stub-engine error), to exercise TokenWatcher's fallback branch.
type erroringEngine struct{}

func (e *erroringEngine) ID() string   { return "broken" }
func (e *erroringEngine) Name() string { return "Broken Engine" }
func (e *erroringEngine) FetchMetrics(ctx context.Context) (*model.Provider, error) {
	return nil, errors.New("simulated engine failure")
}

func TestTokenWatcher_EngineError_FallsBackToUnknownNeverFalseReLogin(t *testing.T) {
	watcher := provider.NewTokenWatcher([]provider.ProviderEngine{&erroringEngine{}}, time.Second)

	dash, err := watcher.PollOnce(context.Background())
	if err != nil {
		t.Fatalf("unexpected error polling watcher: %v", err)
	}
	if len(dash.Providers) != 1 {
		t.Fatalf("expected 1 provider, got %d", len(dash.Providers))
	}

	p := dash.Providers[0]
	if p.AuthState != model.AuthStateUnknown {
		t.Errorf("expected AuthState=unknown on engine error, got %v", p.AuthState)
	}
	if p.ReLoginRequired {
		t.Error("expected ReLoginRequired=false on engine error (never a false re-login)")
	}
	if p.AuthValid {
		t.Error("expected AuthValid=false on engine error")
	}
}

func TestTokenWatcher_PollOnceAndAutoRecovery(t *testing.T) {
	claudeDir := t.TempDir()

	scraper := provider.NewStatusScraper()
	claudeEngine := provider.NewClaudeEngineWithProber(claudeDir, t.TempDir(), scraper, provider.NewAuthProber(http.DefaultClient, alwaysValidProbe))

	watcher := provider.NewTokenWatcher([]provider.ProviderEngine{claudeEngine}, time.Second)

	dash, err := watcher.PollOnce(context.Background())
	if err != nil {
		t.Fatalf("unexpected error polling watcher: %v", err)
	}

	if len(dash.Providers) != 1 {
		t.Fatalf("expected 1 provider, got %d", len(dash.Providers))
	}

	for _, p := range dash.Providers {
		if p.AuthValid {
			t.Errorf("expected AuthValid = false initially for %s", p.ID)
		}
		if !p.ReLoginRequired {
			t.Errorf("expected ReLoginRequired = true initially for %s", p.ID)
		}
	}

	writeClaudeCredentials(t, claudeDir, "sk-ant-oat01-recovered", time.Now().Add(24*time.Hour))

	dashRecovered, err := watcher.PollOnce(context.Background())
	if err != nil {
		t.Fatalf("unexpected error polling watcher after recovery: %v", err)
	}

	var foundClaude bool
	for _, p := range dashRecovered.Providers {
		if p.ID == "claude" {
			foundClaude = true
			if !p.AuthValid {
				t.Error("expected Claude AuthValid = true after recovery")
			}
			if p.ReLoginRequired {
				t.Error("expected Claude ReLoginRequired = false after recovery")
			}
		}
	}
	if !foundClaude {
		t.Error("expected to find claude in providers")
	}
}

func TestTokenWatcher_BackgroundPolling(t *testing.T) {
	claudeDir := t.TempDir()

	scraper := provider.NewStatusScraper()
	claudeEngine := provider.NewClaudeEngineWithProber(claudeDir, t.TempDir(), scraper, provider.NewAuthProber(http.DefaultClient, alwaysValidProbe))

	watcher := provider.NewTokenWatcher([]provider.ProviderEngine{claudeEngine}, 50*time.Millisecond)

	ctx, cancel := context.WithTimeout(context.Background(), 200*time.Millisecond)
	defer cancel()

	updateChan := make(chan struct{}, 5)
	go watcher.Start(ctx, func() {
		updateChan <- struct{}{}
	})

	select {
	case <-updateChan:
	case <-time.After(300 * time.Millisecond):
		t.Error("timed out waiting for background watcher callback")
	}
}

func TestTokenWatcher_PollOnce_RSSBudgetWithLargeClaudeJSON(t *testing.T) {
	claudeDir := t.TempDir()
	home := t.TempDir()

	writeClaudeCredentials(t, claudeDir, "sk-ant-oat01-rss-check", time.Now().Add(24*time.Hour))

	var sb strings.Builder
	sb.WriteString(`{"projects":{`)
	for i := 0; i < 20000; i++ {
		if i > 0 {
			sb.WriteByte(',')
		}
		sb.WriteString(`"/fake/project/path/` + strconv.Itoa(i) + `":{"history":["line one","line two","line three"]}`)
	}
	sb.WriteString(`},"oauthAccount":{"emailAddress":"a@b.com"}}`)
	if err := os.WriteFile(filepath.Join(home, ".claude.json"), []byte(sb.String()), 0600); err != nil {
		t.Fatalf("failed to write large claude.json fixture: %v", err)
	}

	scraper := provider.NewStatusScraper()
	claudeEngine := provider.NewClaudeEngineWithProber(claudeDir, home, scraper, provider.NewAuthProber(http.DefaultClient, alwaysValidProbe))
	watcher := provider.NewTokenWatcher([]provider.ProviderEngine{claudeEngine}, time.Second)

	dash, err := watcher.PollOnce(context.Background())
	if err != nil {
		t.Fatalf("unexpected error polling watcher: %v", err)
	}

	if dash.System.MemoryMB >= 10.0 {
		t.Errorf("expected backend RSS proxy (heap alloc) to stay under 10MB even with a large $HOME/.claude.json, got %.2fMB", dash.System.MemoryMB)
	}
}
