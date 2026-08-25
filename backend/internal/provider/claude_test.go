package provider_test

import (
	"context"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/esp32-ai-monitor/backend/internal/model"
	"github.com/esp32-ai-monitor/backend/internal/provider"
)

func TestClaudeEngine_ValidCredentials(t *testing.T) {
	tempDir := t.TempDir()
	credFile := filepath.Join(tempDir, "credentials.json")

	content := `{
		"session_token": "sk-ant-valid-token-12345",
		"quota_5h": {
			"used": 42.5,
			"limit": 100.0,
			"reset_timestamp": 1756090200
		},
		"quota_weekly": {
			"used": 150.0,
			"limit": 500.0,
			"reset_timestamp": 1756684800
		}
	}`
	if err := os.WriteFile(credFile, []byte(content), 0600); err != nil {
		t.Fatalf("failed to write test cred file: %v", err)
	}

	scraper := provider.NewStatusScraper()
	engine := provider.NewClaudeEngine(tempDir, scraper)

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

	if !p.AuthValid {
		t.Error("expected AuthValid = true")
	}
	if p.ReLoginRequired {
		t.Error("expected ReLoginRequired = false")
	}
	if p.Status != model.StatusOperational {
		t.Errorf("expected Status operational, got %v", p.Status)
	}
	if p.Metrics.Quota5h.Percentage != 42.5 {
		t.Errorf("expected Quota5h percentage 42.5, got %v", p.Metrics.Quota5h.Percentage)
	}
	if p.Metrics.Quota5h.ResetTime == "" {
		t.Error("expected non-empty ResetTime for Quota5h")
	}
	if p.Metrics.QuotaWeekly.Percentage != 30.0 {
		t.Errorf("expected QuotaWeekly percentage 30.0, got %v", p.Metrics.QuotaWeekly.Percentage)
	}
	if p.Metrics.QuotaWeekly.ResetTime == "" {
		t.Error("expected non-empty ResetTime for QuotaWeekly")
	}
}

func TestClaudeEngine_MissingCredentials(t *testing.T) {
	tempDir := t.TempDir() // empty directory

	scraper := provider.NewStatusScraper()
	engine := provider.NewClaudeEngine(tempDir, scraper)

	p, err := engine.FetchMetrics(context.Background())
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}

	if p.AuthValid {
		t.Error("expected AuthValid = false for missing credentials")
	}
	if !p.ReLoginRequired {
		t.Error("expected ReLoginRequired = true for missing credentials")
	}
	if p.Metrics.Quota5h.Percentage != 0.0 {
		t.Errorf("expected Quota5h percentage 0.0, got %v", p.Metrics.Quota5h.Percentage)
	}
}

func TestClaudeEngine_EmptyOrMalformedFile(t *testing.T) {
	tempDir := t.TempDir()
	credFile := filepath.Join(tempDir, "credentials.json")
	if err := os.WriteFile(credFile, []byte("{ malformed json"), 0600); err != nil {
		t.Fatalf("failed to write malformed file: %v", err)
	}

	scraper := provider.NewStatusScraper()
	engine := provider.NewClaudeEngine(tempDir, scraper)

	p, err := engine.FetchMetrics(context.Background())
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}

	if p.AuthValid {
		t.Error("expected AuthValid = false for malformed credentials")
	}
	if !p.ReLoginRequired {
		t.Error("expected ReLoginRequired = true for malformed credentials")
	}
}

func TestClaudeEngine_ExpiredToken(t *testing.T) {
	tempDir := t.TempDir()
	credFile := filepath.Join(tempDir, "credentials.json")
	content := `{
		"session_token": "expired_token_test",
		"expired": true
	}`
	if err := os.WriteFile(credFile, []byte(content), 0600); err != nil {
		t.Fatalf("failed to write expired cred file: %v", err)
	}

	scraper := provider.NewStatusScraper()
	engine := provider.NewClaudeEngine(tempDir, scraper)

	p, err := engine.FetchMetrics(context.Background())
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}

	if p.AuthValid {
		t.Error("expected AuthValid = false for expired token")
	}
	if !p.ReLoginRequired {
		t.Error("expected ReLoginRequired = true for expired token")
	}
}

func TestClaudeEngine_DefaultQuotaCalculations(t *testing.T) {
	tempDir := t.TempDir()
	credFile := filepath.Join(tempDir, "credentials.json")
	content := `{
		"session_token": "sk-ant-valid-token-only"
	}`
	if err := os.WriteFile(credFile, []byte(content), 0600); err != nil {
		t.Fatalf("failed to write cred file: %v", err)
	}

	scraper := provider.NewStatusScraper()
	engine := provider.NewClaudeEngine(tempDir, scraper)

	p, err := engine.FetchMetrics(context.Background())
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}

	if !p.AuthValid {
		t.Error("expected AuthValid = true")
	}
	if p.Metrics.Quota5h.Limit <= 0 {
		t.Errorf("expected positive limit, got %v", p.Metrics.Quota5h.Limit)
	}
	if p.Metrics.Quota5h.ResetTimestamp <= time.Now().Unix() {
		t.Error("expected future reset timestamp")
	}
}
