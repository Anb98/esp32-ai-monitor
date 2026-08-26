package provider

import (
	"context"
	"runtime"
	"time"

	"github.com/esp32-ai-monitor/backend/internal/model"
)

var startTime = time.Now()

type TokenWatcher struct {
	engines      []ProviderEngine
	pollInterval time.Duration
}

func NewTokenWatcher(engines []ProviderEngine, pollInterval time.Duration) *TokenWatcher {
	return &TokenWatcher{
		engines:      engines,
		pollInterval: pollInterval,
	}
}

func (w *TokenWatcher) PollOnce(ctx context.Context) (*model.DashboardResponse, error) {
	providers := make([]model.Provider, 0, len(w.engines))

	for _, eng := range w.engines {
		p, err := eng.FetchMetrics(ctx)
		if err != nil {
			// Engine failure proves nothing about auth validity: unknown,
			// never a false re-login prompt.
			p = &model.Provider{
				ID:     eng.ID(),
				Name:   eng.Name(),
				Status: model.StatusDegraded,
				Stale:  true,
			}
			p.ApplyAuthState(model.AuthStateUnknown)
		}
		providers = append(providers, *p)
	}

	var m runtime.MemStats
	runtime.ReadMemStats(&m)
	memoryMB := float64(m.Alloc) / (1024 * 1024)

	dash := &model.DashboardResponse{
		Timestamp: time.Now().Unix(),
		Providers: providers,
		System: model.SystemMetrics{
			UptimeSeconds: int64(time.Since(startTime).Seconds()),
			MemoryMB:      memoryMB,
		},
	}

	return dash, nil
}

func (w *TokenWatcher) Start(ctx context.Context, onUpdate func()) {
	ticker := time.NewTicker(w.pollInterval)
	defer ticker.Stop()

	// Initial poll immediately
	if onUpdate != nil {
		onUpdate()
	}

	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			if onUpdate != nil {
				onUpdate()
			}
		}
	}
}
