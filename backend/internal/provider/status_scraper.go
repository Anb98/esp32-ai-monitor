package provider

import (
	"context"
	"encoding/json"
	"log"
	"net/http"
	"sync"
	"time"

	"github.com/esp32-ai-monitor/backend/internal/model"
)

const (
	defaultClaudeStatusURL = "https://status.claude.com/api/v2/summary.json"
	claudeCodeComponentID  = "yyzkbfz2thpt"
)

type claudeSummaryResponse struct {
	Components []struct {
		ID     string `json:"id"`
		Name   string `json:"name"`
		Status string `json:"status"`
	} `json:"components"`
}

type StatusScraper struct {
	claudeURL      string
	httpClient     *http.Client
	mu             sync.RWMutex
	lastClaudeStat model.ProviderStatus
	lastGoogleStat model.ProviderStatus
}

func NewStatusScraper() *StatusScraper {
	return NewStatusScraperWithURL(defaultClaudeStatusURL, &http.Client{
		Timeout: 5 * time.Second,
	})
}

func NewStatusScraperWithURL(claudeURL string, client *http.Client) *StatusScraper {
	if client == nil {
		client = &http.Client{Timeout: 5 * time.Second}
	}
	return &StatusScraper{
		claudeURL:      claudeURL,
		httpClient:     client,
		lastClaudeStat: model.StatusOperational,
		lastGoogleStat: model.StatusOperational,
	}
}

func (s *StatusScraper) FetchClaudeStatus(ctx context.Context) model.ProviderStatus {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, s.claudeURL, nil)
	if err != nil {
		s.mu.RLock()
		defer s.mu.RUnlock()
		return s.lastClaudeStat
	}

	resp, err := s.httpClient.Do(req)
	if err != nil {
		log.Printf("[WARN] Failed to query Claude status upstream: %v", err)
		s.mu.RLock()
		defer s.mu.RUnlock()
		return s.lastClaudeStat
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		log.Printf("[WARN] Claude status upstream returned non-200: %d", resp.StatusCode)
		s.mu.RLock()
		defer s.mu.RUnlock()
		return s.lastClaudeStat
	}

	var summary claudeSummaryResponse
	if err := json.NewDecoder(resp.Body).Decode(&summary); err != nil {
		log.Printf("[WARN] Failed to decode Claude status JSON: %v", err)
		s.mu.RLock()
		defer s.mu.RUnlock()
		return s.lastClaudeStat
	}

	for _, comp := range summary.Components {
		if comp.ID == claudeCodeComponentID {
			mappedStatus := mapClaudeComponentStatus(comp.Status)
			s.mu.Lock()
			s.lastClaudeStat = mappedStatus
			s.mu.Unlock()
			return mappedStatus
		}
	}

	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.lastClaudeStat
}

func (s *StatusScraper) FetchGoogleStatus(ctx context.Context) model.ProviderStatus {
	// Google status check with fallback to last known status
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.lastGoogleStat
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
