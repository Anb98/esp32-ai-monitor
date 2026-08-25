package cache

import (
	"sync"
	"time"

	"github.com/esp32-ai-monitor/backend/internal/model"
)

type Cache struct {
	mu   sync.RWMutex
	data *model.DashboardResponse
}

func New() *Cache {
	return &Cache{
		data: &model.DashboardResponse{
			Timestamp: time.Now().Unix(),
			Providers: []model.Provider{},
			System: model.SystemMetrics{
				UptimeSeconds: 0,
				MemoryMB:      0,
			},
		},
	}
}

func (c *Cache) Set(data *model.DashboardResponse) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.data = data
}

func (c *Cache) Get() *model.DashboardResponse {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.data
}
