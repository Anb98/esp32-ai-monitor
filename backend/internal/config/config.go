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

	return &Config{
		Port:            port,
		PollInterval:    30 * time.Second,
		ClaudeConfigDir: claudeDir,
	}
}
