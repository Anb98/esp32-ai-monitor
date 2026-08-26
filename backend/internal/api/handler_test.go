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

func TestHandleDashboard_GoldenJSON_AdditiveFieldsAndLegacyDerivation(t *testing.T) {
	c := cache.New()

	claude := model.Provider{
		ID:     "claude",
		Name:   "Claude Code",
		Status: model.StatusOperational,
		Stale:  false,
		Metrics: model.ProviderMetrics{
			Quota5h: model.QuotaWindow{
				Used: 42.5, Limit: 100.0, Percentage: 42.5,
				ResetTime: "15:30", ResetTimestamp: 1756090200,
				Available: true, ResetInSeconds: 3600,
			},
			QuotaWeekly: model.QuotaWindow{
				Available: false,
			},
		},
	}
	claude.ApplyAuthState(model.AuthStateUnknown)

	c.Set(&model.DashboardResponse{
		Timestamp: 1756083867,
		Providers: []model.Provider{claude},
		System:    model.SystemMetrics{UptimeSeconds: 3600, MemoryMB: 4.8},
	})

	h := api.NewHandler(c)
	router := api.NewRouter(h)

	req := httptest.NewRequest(http.MethodGet, "/api/dashboard", nil)
	rec := httptest.NewRecorder()
	router.ServeHTTP(rec, req)

	if rec.Code != http.StatusOK {
		t.Fatalf("expected status 200, got %d", rec.Code)
	}

	var raw map[string]any
	if err := json.Unmarshal(rec.Body.Bytes(), &raw); err != nil {
		t.Fatalf("failed to decode json: %v", err)
	}

	providers, ok := raw["providers"].([]any)
	if !ok || len(providers) != 1 {
		t.Fatalf("expected 1 provider in raw payload, got %v", raw["providers"])
	}
	p, ok := providers[0].(map[string]any)
	if !ok {
		t.Fatalf("expected provider to decode as an object")
	}

	// Legacy fields (unchanged type/meaning) must still be present.
	for _, field := range []string{"id", "name", "status", "auth_valid", "re_login_required", "metrics"} {
		if _, present := p[field]; !present {
			t.Errorf("expected legacy field %q to still be present", field)
		}
	}

	// auth_state=unknown MUST derive auth_valid=false AND re_login_required=false (never a false re-login).
	if p["auth_state"] != "unknown" {
		t.Errorf("expected auth_state=unknown, got %v", p["auth_state"])
	}
	if p["auth_valid"] != false {
		t.Errorf("expected auth_valid=false for auth_state=unknown, got %v", p["auth_valid"])
	}
	if p["re_login_required"] != false {
		t.Errorf("expected re_login_required=false for auth_state=unknown, got %v", p["re_login_required"])
	}

	if _, present := p["auth_checked_at"]; !present {
		t.Error("expected additive field auth_checked_at to be present")
	}
	if _, present := p["stale"]; !present {
		t.Error("expected additive field stale to be present")
	}

	metrics, ok := p["metrics"].(map[string]any)
	if !ok {
		t.Fatalf("expected metrics to decode as an object")
	}
	q5h, ok := metrics["quota_5h"].(map[string]any)
	if !ok {
		t.Fatalf("expected quota_5h to decode as an object")
	}
	if q5h["available"] != true {
		t.Errorf("expected quota_5h.available=true, got %v", q5h["available"])
	}
	if q5h["reset_in_seconds"] != float64(3600) {
		t.Errorf("expected quota_5h.reset_in_seconds=3600, got %v", q5h["reset_in_seconds"])
	}

	qWk, ok := metrics["quota_weekly"].(map[string]any)
	if !ok {
		t.Fatalf("expected quota_weekly to decode as an object")
	}
	if qWk["available"] != false {
		t.Errorf("expected quota_weekly.available=false when no live source exists, got %v", qWk["available"])
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
