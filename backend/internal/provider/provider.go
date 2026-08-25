package provider

import (
	"context"

	"github.com/esp32-ai-monitor/backend/internal/model"
)

// ProviderEngine defines the interface for collecting metrics and status from an AI provider.
type ProviderEngine interface {
	ID() string
	Name() string
	FetchMetrics(ctx context.Context) (*model.Provider, error)
}
