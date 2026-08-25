package api_test

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"sync"
	"testing"
	"time"

	"github.com/esp32-ai-monitor/backend/internal/api"
	"github.com/esp32-ai-monitor/backend/internal/cache"
	"github.com/esp32-ai-monitor/backend/internal/model"
)

func TestHandleHealth(t *testing.T) {
	c := cache.New()
	h := api.NewHandler(c)
	router := api.NewRouter(h)

	req := httptest.NewRequest(http.MethodGet, "/healthz", nil)
	rec := httptest.NewRecorder()

	router.ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Errorf("expected status 200, got %d", rec.Code)
	}

	contentType := rec.Header().Get("Content-Type")
	if contentType != "application/json" {
		t.Errorf("expected Content-Type application/json, got %s", contentType)
	}

	var resp map[string]string
	if err := json.NewDecoder(rec.Body).Decode(&resp); err != nil {
		t.Fatalf("failed to decode json: %v", err)
	}

	if resp["status"] != "ok" {
		t.Errorf("expected status 'ok', got '%s'", resp["status"])
	}
}

func TestHandleDashboard(t *testing.T) {
	c := cache.New()
	c.Set(&model.DashboardResponse{
		Timestamp: 1756083867,
		Providers: []model.Provider{
			{
				ID:              "claude",
				Name:            "Claude Code",
				Status:          model.StatusOperational,
				AuthValid:       true,
				ReLoginRequired: false,
				Metrics: model.ProviderMetrics{
					Quota5h: model.QuotaWindow{
						Used:           42.5,
						Limit:          100.0,
						Percentage:     42.5,
						ResetTime:      "15:30",
						ResetTimestamp: 1756090200,
					},
					QuotaWeekly: model.QuotaWindow{
						Used:           150.0,
						Limit:          500.0,
						Percentage:     30.0,
						ResetTime:      "Dom 00:00",
						ResetTimestamp: 1756684800,
					},
				},
			},
		},
		System: model.SystemMetrics{
			UptimeSeconds: 3600,
			MemoryMB:      4.8,
		},
	})

	h := api.NewHandler(c)
	router := api.NewRouter(h)

	req := httptest.NewRequest(http.MethodGet, "/api/dashboard", nil)
	rec := httptest.NewRecorder()

	start := time.Now()
	router.ServeHTTP(rec, req)
	duration := time.Since(start)

	if rec.Code != http.StatusOK {
		t.Errorf("expected status 200, got %d", rec.Code)
	}

	if duration > 10*time.Millisecond {
		t.Errorf("expected response latency <10ms, took %v", duration)
	}

	contentType := rec.Header().Get("Content-Type")
	if contentType != "application/json" {
		t.Errorf("expected Content-Type application/json, got %s", contentType)
	}

	// Verify CORS header
	corsHeader := rec.Header().Get("Access-Control-Allow-Origin")
	if corsHeader != "*" {
		t.Errorf("expected Access-Control-Allow-Origin '*', got '%s'", corsHeader)
	}

	var dash model.DashboardResponse
	if err := json.NewDecoder(rec.Body).Decode(&dash); err != nil {
		t.Fatalf("failed to decode json: %v", err)
	}

	if dash.Timestamp != 1756083867 {
		t.Errorf("expected timestamp 1756083867, got %d", dash.Timestamp)
	}
	if len(dash.Providers) != 1 {
		t.Fatalf("expected 1 provider, got %d", len(dash.Providers))
	}
	if dash.Providers[0].Metrics.Quota5h.Percentage != 42.5 {
		t.Errorf("expected 42.5%%, got %v", dash.Providers[0].Metrics.Quota5h.Percentage)
	}
}

func TestConcurrentDashboardRequests(t *testing.T) {
	c := cache.New()
	h := api.NewHandler(c)
	router := api.NewRouter(h)

	var wg sync.WaitGroup
	for i := 0; i < 50; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			req := httptest.NewRequest(http.MethodGet, "/api/dashboard", nil)
			rec := httptest.NewRecorder()
			router.ServeHTTP(rec, req)
			if rec.Code != http.StatusOK {
				t.Errorf("expected status 200, got %d", rec.Code)
			}
		}()
	}
	wg.Wait()
}

func TestHandler_MethodNotAllowedAndOptions(t *testing.T) {
	c := cache.New()
	h := api.NewHandler(c)
	router := api.NewRouter(h)

	// POST /api/dashboard should return 405
	req := httptest.NewRequest(http.MethodPost, "/api/dashboard", nil)
	rec := httptest.NewRecorder()
	router.ServeHTTP(rec, req)
	if rec.Code != http.StatusMethodNotAllowed {
		t.Errorf("expected 405 Method Not Allowed, got %d", rec.Code)
	}

	// POST /healthz should return 405
	reqH := httptest.NewRequest(http.MethodPost, "/healthz", nil)
	recH := httptest.NewRecorder()
	router.ServeHTTP(recH, reqH)
	if recH.Code != http.StatusMethodNotAllowed {
		t.Errorf("expected 405 Method Not Allowed, got %d", recH.Code)
	}

	// OPTIONS /api/dashboard should return 200 (CORS preflight)
	reqOpt := httptest.NewRequest(http.MethodOptions, "/api/dashboard", nil)
	recOpt := httptest.NewRecorder()
	router.ServeHTTP(recOpt, reqOpt)
	if recOpt.Code != http.StatusOK {
		t.Errorf("expected 200 OK for OPTIONS, got %d", recOpt.Code)
	}
}

func TestRecoveryMiddleware(t *testing.T) {
	panicHandler := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		panic("simulated panic")
	})
	recovered := api.RecoveryMiddleware(panicHandler)

	req := httptest.NewRequest(http.MethodGet, "/panic", nil)
	rec := httptest.NewRecorder()
	recovered.ServeHTTP(rec, req)

	if rec.Code != http.StatusInternalServerError {
		t.Errorf("expected 500 on panic, got %d", rec.Code)
	}
}
