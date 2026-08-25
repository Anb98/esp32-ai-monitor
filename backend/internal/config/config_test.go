package config_test

import (
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/esp32-ai-monitor/backend/internal/config"
)

func TestLoadConfig_Defaults(t *testing.T) {
	// Clear env vars
	os.Unsetenv("PORT")
	os.Unsetenv("CLAUDE_CONFIG_DIR")
	os.Unsetenv("GEMINI_CONFIG_DIR")

	cfg := config.LoadConfig()
	if cfg == nil {
		t.Fatal("expected non-nil config")
	}

	if cfg.Port != "8080" {
		t.Errorf("expected default Port 8080, got %s", cfg.Port)
	}

	if cfg.PollInterval != 30*time.Second {
		t.Errorf("expected default PollInterval 30s, got %v", cfg.PollInterval)
	}

	home, err := os.UserHomeDir()
	if err == nil && home != "" {
		expectedClaude := filepath.Join(home, ".claude")
		if cfg.ClaudeConfigDir != expectedClaude {
			t.Errorf("expected ClaudeConfigDir %s, got %s", expectedClaude, cfg.ClaudeConfigDir)
		}

		expectedGemini := filepath.Join(home, ".gemini")
		if cfg.GeminiConfigDir != expectedGemini {
			t.Errorf("expected GeminiConfigDir %s, got %s", expectedGemini, cfg.GeminiConfigDir)
		}
	}
}

func TestLoadConfig_EnvOverrides(t *testing.T) {
	t.Setenv("PORT", "9090")
	t.Setenv("CLAUDE_CONFIG_DIR", "/custom/claude")
	t.Setenv("GEMINI_CONFIG_DIR", "/custom/gemini")

	cfg := config.LoadConfig()
	if cfg == nil {
		t.Fatal("expected non-nil config")
	}

	if cfg.Port != "9090" {
		t.Errorf("expected Port 9090, got %s", cfg.Port)
	}

	if cfg.ClaudeConfigDir != "/custom/claude" {
		t.Errorf("expected ClaudeConfigDir /custom/claude, got %s", cfg.ClaudeConfigDir)
	}

	if cfg.GeminiConfigDir != "/custom/gemini" {
		t.Errorf("expected GeminiConfigDir /custom/gemini, got %s", cfg.GeminiConfigDir)
	}
}
