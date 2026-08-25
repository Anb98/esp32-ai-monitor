package provider_test

import (
	"context"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/esp32-ai-monitor/backend/internal/model"
	"github.com/esp32-ai-monitor/backend/internal/provider"
)

func TestClaudeStatusScraper_ComponentFiltering(t *testing.T) {
	tests := []struct {
		name           string
		componentState string
		expectedStatus model.ProviderStatus
	}{
		{
			name:           "Operational component",
			componentState: "operational",
			expectedStatus: model.StatusOperational,
		},
		{
			name:           "Degraded performance component",
			componentState: "degraded_performance",
			expectedStatus: model.StatusDegraded,
		},
		{
			name:           "Under maintenance component",
			componentState: "under_maintenance",
			expectedStatus: model.StatusDegraded,
		},
		{
			name:           "Major outage component",
			componentState: "major_outage",
			expectedStatus: model.StatusOutage,
		},
		{
			name:           "Partial outage component",
			componentState: "partial_outage",
			expectedStatus: model.StatusOutage,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				w.Header().Set("Content-Type", "application/json")
				w.WriteHeader(http.StatusOK)
				_, _ = w.Write([]byte(`{
					"components": [
						{"id": "other-comp", "name": "Web UI", "status": "major_outage"},
						{"id": "yyzkbfz2thpt", "name": "Claude Code", "status": "` + tt.componentState + `"}
					]
				}`))
			}))
			defer server.Close()

			scraper := provider.NewStatusScraperWithURL(server.URL, server.Client())
			status := scraper.FetchClaudeStatus(context.Background())

			if status != tt.expectedStatus {
				t.Errorf("expected status %v, got %v", tt.expectedStatus, status)
			}
		})
	}
}

func TestClaudeStatusScraper_NetworkFailureRetainsLastKnown(t *testing.T) {
	// First successful call: degraded
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write([]byte(`{
			"components": [
				{"id": "yyzkbfz2thpt", "name": "Claude Code", "status": "degraded_performance"}
			]
		}`))
	}))
	defer server.Close()

	scraper := provider.NewStatusScraperWithURL(server.URL, server.Client())
	status := scraper.FetchClaudeStatus(context.Background())
	if status != model.StatusDegraded {
		t.Fatalf("expected initial status degraded, got %v", status)
	}

	// Now point to failing server or close server
	server.Close()

	// Subsequent call should retain last known valid status without panic
	recoveredStatus := scraper.FetchClaudeStatus(context.Background())
	if recoveredStatus != model.StatusDegraded {
		t.Errorf("expected retained status degraded, got %v", recoveredStatus)
	}
}

func TestGoogleStatusScraper(t *testing.T) {
	scraper := provider.NewStatusScraper()
	status := scraper.FetchGoogleStatus(context.Background())
	if status != model.StatusOperational && status != model.StatusDegraded && status != model.StatusOutage {
		t.Errorf("unexpected status %v", status)
	}
}
