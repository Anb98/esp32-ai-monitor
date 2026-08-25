package cache_test

import (
	"sync"
	"testing"

	"github.com/esp32-ai-monitor/backend/internal/cache"
	"github.com/esp32-ai-monitor/backend/internal/model"
)

func TestCache_SetAndGet(t *testing.T) {
	c := cache.New()

	// Initial get on empty cache
	initial := c.Get()
	if initial == nil {
		t.Fatal("expected non-nil default response from empty cache")
	}

	data := &model.DashboardResponse{
		Timestamp: 1756083867,
		Providers: []model.Provider{
			{
				ID:     "claude",
				Name:   "Claude Code",
				Status: model.StatusOperational,
			},
		},
		System: model.SystemMetrics{
			UptimeSeconds: 120,
			MemoryMB:      4.2,
		},
	}

	c.Set(data)

	retrieved := c.Get()
	if retrieved == nil {
		t.Fatal("expected non-nil data")
	}
	if retrieved.Timestamp != 1756083867 {
		t.Errorf("expected timestamp 1756083867, got %d", retrieved.Timestamp)
	}
	if len(retrieved.Providers) != 1 || retrieved.Providers[0].ID != "claude" {
		t.Errorf("unexpected providers in cache: %+v", retrieved.Providers)
	}
	if retrieved.System.MemoryMB != 4.2 {
		t.Errorf("expected memory 4.2, got %v", retrieved.System.MemoryMB)
	}
}

func TestCache_ConcurrentAccess(t *testing.T) {
	c := cache.New()
	var wg sync.WaitGroup

	// Start 50 concurrent readers
	for i := 0; i < 50; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for j := 0; j < 100; j++ {
				_ = c.Get()
			}
		}()
	}

	// Start 10 concurrent writers
	for i := 0; i < 10; i++ {
		wg.Add(1)
		go func(writerID int) {
			defer wg.Done()
			for j := 0; j < 100; j++ {
				c.Set(&model.DashboardResponse{
					Timestamp: int64(writerID*1000 + j),
					Providers: []model.Provider{},
				})
			}
		}(i)
	}

	wg.Wait()

	final := c.Get()
	if final == nil {
		t.Fatal("expected non-nil cache value after concurrent ops")
	}
}
