package provider

import (
	"context"
	"encoding/json"
	"io"
	"log"
	"net/http"
	"sync"
	"time"

	"github.com/esp32-ai-monitor/backend/internal/model"
)

const (
	defaultClaudeStatusURL = "https://status.claude.com/api/v2/summary.json"
	claudeCodeComponentID  = "yyzkbfz2thpt"

	statusFeedBodyLimit = 256 * 1024
)

type claudeSummaryResponse struct {
	Components []struct {
		ID     string `json:"id"`
		Name   string `json:"name"`
		Status string `json:"status"`
	} `json:"components"`
}

type StatusScraper struct {
	claudeURL string

	httpClient *http.Client

	mu             sync.RWMutex
	lastClaudeStat model.ProviderStatus
}

func NewStatusScraper() *StatusScraper {
	return NewStatusScraperWithURLs(defaultClaudeStatusURL, &http.Client{
		Timeout: 5 * time.Second,
	})
}

func NewStatusScraperWithURLs(claudeURL string, client *http.Client) *StatusScraper {
	if client == nil {
		client = &http.Client{Timeout: 5 * time.Second}
	}
	return &StatusScraper{
		claudeURL:      claudeURL,
		httpClient:     client,
		lastClaudeStat: model.StatusOperational,
	}
}

// FetchClaudeStatus queries status.claude.com and filters to the Claude Code
// component. On any fetch/decode failure it returns the last known status
// with stale=true rather than crashing or guessing.
func (s *StatusScraper) FetchClaudeStatus(ctx context.Context) (model.ProviderStatus, bool) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, s.claudeURL, nil)
	if err != nil {
		return s.lastClaude(), true
	}

	resp, err := s.httpClient.Do(req)
	if err != nil {
		log.Printf("[WARN] Failed to query Claude status upstream: %v", err)
		return s.lastClaude(), true
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		log.Printf("[WARN] Claude status upstream returned non-200: %d", resp.StatusCode)
		return s.lastClaude(), true
	}

	var summary claudeSummaryResponse
	if err := json.NewDecoder(io.LimitReader(resp.Body, statusFeedBodyLimit)).Decode(&summary); err != nil {
		log.Printf("[WARN] Failed to decode Claude status JSON: %v", err)
		return s.lastClaude(), true
	}

	for _, comp := range summary.Components {
		if comp.ID == claudeCodeComponentID {
			mapped := mapClaudeComponentStatus(comp.Status)
			s.mu.Lock()
			s.lastClaudeStat = mapped
			s.mu.Unlock()
			return mapped, false
		}
	}

	return s.lastClaude(), true
}

func (s *StatusScraper) lastClaude() model.ProviderStatus {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.lastClaudeStat
}

func mapClaudeComponentStatus(status string) model.ProviderStatus {
	switch status {
	case "operational":
		return model.StatusOperational
	case "degraded_performance", "under_maintenance":
		return model.StatusDegraded
	case "major_outage", "partial_outage":
		return model.StatusOutage
	default:
		return model.StatusOperational
	}
}
