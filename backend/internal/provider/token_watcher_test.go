package provider_test

import (
	"context"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/esp32-ai-monitor/backend/internal/provider"
)

func TestTokenWatcher_PollOnceAndAutoRecovery(t *testing.T) {
	claudeDir := t.TempDir()
	geminiDir := t.TempDir()

	scraper := provider.NewStatusScraper()
	claudeEngine := provider.NewClaudeEngine(claudeDir, scraper)
	geminiEngine := provider.NewAntigravityEngine(geminiDir, scraper)

	watcher := provider.NewTokenWatcher([]provider.ProviderEngine{claudeEngine, geminiEngine}, time.Second)

	// Initially no credentials -> both invalid
	dash, err := watcher.PollOnce(context.Background())
	if err != nil {
		t.Fatalf("unexpected error polling watcher: %v", err)
	}

	if len(dash.Providers) != 2 {
		t.Fatalf("expected 2 providers, got %d", len(dash.Providers))
	}

	for _, p := range dash.Providers {
		if p.AuthValid {
			t.Errorf("expected AuthValid = false initially for %s", p.ID)
		}
		if !p.ReLoginRequired {
			t.Errorf("expected ReLoginRequired = true initially for %s", p.ID)
		}
	}

	// Now simulate user logging in with Claude CLI
	claudeCredFile := filepath.Join(claudeDir, "credentials.json")
	if err := os.WriteFile(claudeCredFile, []byte(`{"session_token":"valid_claude_login"}`), 0600); err != nil {
		t.Fatalf("failed to write claude cred: %v", err)
	}

	// Poll again -> Claude should recover automatically
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
	geminiDir := t.TempDir()

	scraper := provider.NewStatusScraper()
	claudeEngine := provider.NewClaudeEngine(claudeDir, scraper)
	geminiEngine := provider.NewAntigravityEngine(geminiDir, scraper)

	watcher := provider.NewTokenWatcher([]provider.ProviderEngine{claudeEngine, geminiEngine}, 50*time.Millisecond)

	ctx, cancel := context.WithTimeout(context.Background(), 200*time.Millisecond)
	defer cancel()

	updateChan := make(chan struct{}, 5)
	go watcher.Start(ctx, func() {
		updateChan <- struct{}{}
	})

	select {
	case <-updateChan:
		// Succeeded in receiving at least one background poll update
	case <-time.After(300 * time.Millisecond):
		t.Error("timed out waiting for background watcher callback")
	}
}
