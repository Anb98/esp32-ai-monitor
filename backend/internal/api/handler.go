package api

import (
	"encoding/json"
	"net/http"

	"github.com/esp32-ai-monitor/backend/internal/cache"
)

type Handler struct {
	cache *cache.Cache
}

func NewHandler(c *cache.Cache) *Handler {
	return &Handler{
		cache: c,
	}
}

func (h *Handler) HandleDashboard(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet && r.Method != http.MethodOptions {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)

	data := h.cache.Get()
	_ = json.NewEncoder(w).Encode(data)
}

func (h *Handler) HandleHealth(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet && r.Method != http.MethodOptions {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)
	_, _ = w.Write([]byte(`{"status":"ok"}`))
}
