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

func TestAntigravityEngine_ValidCredentials(t *testing.T) {
	tempDir := t.TempDir()
	credFile := filepath.Join(tempDir, "credentials.json")

	content := `{
		"access_token": "ya29.antigravity-valid-token-67890",
		"quota_5h": {
			"used": 12.0,
			"limit": 100.0,
			"reset_timestamp": 1756099200
		},
		"quota_weekly": {
			"used": 80.0,
			"limit": 1000.0,
			"reset_timestamp": 1756771200
		}
	}`
	if err := os.WriteFile(credFile, []byte(content), 0600); err != nil {
		t.Fatalf("failed to write test cred file: %v", err)
	}

	scraper := provider.NewStatusScraper()
	engine := provider.NewAntigravityEngine(tempDir, scraper)

	if engine.ID() != "antigravity" {
		t.Errorf("expected ID 'antigravity', got %s", engine.ID())
	}
	if engine.Name() != "Google Antigravity" {
		t.Errorf("expected Name 'Google Antigravity', got %s", engine.Name())
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
	if p.Metrics.Quota5h.Percentage != 12.0 {
		t.Errorf("expected Quota5h percentage 12.0, got %v", p.Metrics.Quota5h.Percentage)
	}
	if p.Metrics.Quota5h.ResetTime == "" {
		t.Error("expected non-empty ResetTime for Quota5h")
	}
	if p.Metrics.QuotaWeekly.Percentage != 8.0 {
		t.Errorf("expected QuotaWeekly percentage 8.0, got %v", p.Metrics.QuotaWeekly.Percentage)
	}
	if p.Metrics.QuotaWeekly.ResetTime == "" {
		t.Error("expected non-empty ResetTime for QuotaWeekly")
	}
}

func TestAntigravityEngine_MissingCredentials(t *testing.T) {
	tempDir := t.TempDir()

	scraper := provider.NewStatusScraper()
	engine := provider.NewAntigravityEngine(tempDir, scraper)

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

func TestAntigravityEngine_MalformedOrExpired(t *testing.T) {
	tempDir := t.TempDir()
	credFile := filepath.Join(tempDir, "credentials.json")
	if err := os.WriteFile(credFile, []byte("{ malformed json"), 0600); err != nil {
		t.Fatalf("failed to write malformed file: %v", err)
	}

	scraper := provider.NewStatusScraper()
	engine := provider.NewAntigravityEngine(tempDir, scraper)

	p, err := engine.FetchMetrics(context.Background())
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}

	if p.AuthValid {
		t.Error("expected AuthValid = false")
	}
	if !p.ReLoginRequired {
		t.Error("expected ReLoginRequired = true")
	}

	// Test expired
	if err := os.WriteFile(credFile, []byte(`{"access_token":"tok","expired":true}`), 0600); err != nil {
		t.Fatalf("failed to write expired file: %v", err)
	}
	pExpired, _ := engine.FetchMetrics(context.Background())
	if pExpired.AuthValid || !pExpired.ReLoginRequired {
		t.Error("expected auth invalid for expired token")
	}
}

func TestAntigravityEngine_DefaultQuotaCalculation(t *testing.T) {
	tempDir := t.TempDir()
	credFile := filepath.Join(tempDir, "credentials.json")
	if err := os.WriteFile(credFile, []byte(`{"access_token":"valid_tok"}`), 0600); err != nil {
		t.Fatalf("failed to write file: %v", err)
	}

	scraper := provider.NewStatusScraper()
	engine := provider.NewAntigravityEngine(tempDir, scraper)

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
