package config

import (
	"os"
	"path/filepath"
	"time"
)

type Config struct {
	Port            string
	PollInterval    time.Duration
	ClaudeConfigDir string
	GeminiConfigDir string
}

func LoadConfig() *Config {
	homeDir, _ := os.UserHomeDir()

	port := os.Getenv("PORT")
	if port == "" {
		port = "8080"
	}

	claudeDir := os.Getenv("CLAUDE_CONFIG_DIR")
	if claudeDir == "" {
		if homeDir != "" {
			claudeDir = filepath.Join(homeDir, ".claude")
		} else {
			claudeDir = ".claude"
		}
	}

	geminiDir := os.Getenv("GEMINI_CONFIG_DIR")
	if geminiDir == "" {
		if homeDir != "" {
			geminiDir = filepath.Join(homeDir, ".gemini")
		} else {
			geminiDir = ".gemini"
		}
	}

	return &Config{
		Port:            port,
		PollInterval:    30 * time.Second,
		ClaudeConfigDir: claudeDir,
		GeminiConfigDir: geminiDir,
	}
}
