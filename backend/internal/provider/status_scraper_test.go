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
		{"Operational component", "operational", model.StatusOperational},
		{"Degraded performance component", "degraded_performance", model.StatusDegraded},
		{"Under maintenance component", "under_maintenance", model.StatusDegraded},
		{"Major outage component", "major_outage", model.StatusOutage},
		{"Partial outage component", "partial_outage", model.StatusOutage},
		{"Unrecognized status defaults to operational", "some_future_status", model.StatusOperational},
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

			scraper := provider.NewStatusScraperWithURLs(server.URL, server.Client())
			status, stale := scraper.FetchClaudeStatus(context.Background())

			if status != tt.expectedStatus {
				t.Errorf("expected status %v, got %v", tt.expectedStatus, status)
			}
			if stale {
				t.Error("expected stale=false for a successful fetch")
			}
		})
	}
}

func TestClaudeStatusScraper_NetworkFailureRetainsLastKnownAndMarksStale(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write([]byte(`{
			"components": [
				{"id": "yyzkbfz2thpt", "name": "Claude Code", "status": "degraded_performance"}
			]
		}`))
	}))

	scraper := provider.NewStatusScraperWithURLs(server.URL, server.Client())
	status, stale := scraper.FetchClaudeStatus(context.Background())
	if status != model.StatusDegraded || stale {
		t.Fatalf("expected initial status degraded/not-stale, got status=%v stale=%v", status, stale)
	}

	server.Close()

	recoveredStatus, recoveredStale := scraper.FetchClaudeStatus(context.Background())
	if recoveredStatus != model.StatusDegraded {
		t.Errorf("expected retained status degraded, got %v", recoveredStatus)
	}
	if !recoveredStale {
		t.Error("expected stale=true after a failed fetch that falls back to last known")
	}
}

func TestNewStatusScraper_ConstructsWithDefaultURLs(t *testing.T) {
	scraper := provider.NewStatusScraper()
	if scraper == nil {
		t.Fatal("expected a non-nil StatusScraper")
	}
}

func TestNewStatusScraperWithURLs_NilClientDefaults(t *testing.T) {
	scraper := provider.NewStatusScraperWithURLs("https://example.invalid", nil)
	if scraper == nil {
		t.Fatal("expected a non-nil StatusScraper when client is nil")
	}
}

func TestClaudeStatusScraper_MalformedJSONRetainsLastKnownAndMarksStale(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write([]byte(`{not valid json`))
	}))
	defer server.Close()

	scraper := provider.NewStatusScraperWithURLs(server.URL, server.Client())
	status, stale := scraper.FetchClaudeStatus(context.Background())

	if status != model.StatusOperational {
		t.Errorf("expected default last-known operational on malformed JSON, got %v", status)
	}
	if !stale {
		t.Error("expected stale=true on malformed JSON")
	}
}

func TestClaudeStatusScraper_Non200RetainsLastKnownAndMarksStale(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusServiceUnavailable)
	}))
	defer server.Close()

	scraper := provider.NewStatusScraperWithURLs(server.URL, server.Client())
	status, stale := scraper.FetchClaudeStatus(context.Background())

	if status != model.StatusOperational {
		t.Errorf("expected default last-known operational on non-200, got %v", status)
	}
	if !stale {
		t.Error("expected stale=true on non-200 response")
	}
}

func TestClaudeStatusScraper_ComponentNotFoundFallsThroughToLastKnown(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write([]byte(`{"components":[{"id":"unrelated","name":"Other","status":"major_outage"}]}`))
	}))
	defer server.Close()

	scraper := provider.NewStatusScraperWithURLs(server.URL, server.Client())
	status, stale := scraper.FetchClaudeStatus(context.Background())

	if status != model.StatusOperational {
		t.Errorf("expected default last-known operational when the Claude Code component is absent, got %v", status)
	}
	if !stale {
		t.Error("expected stale=true when the component of interest is not present in the response")
	}
}
