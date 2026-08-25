package api

import (
	"net/http"
)

func NewRouter(h *Handler) http.Handler {
	mux := http.NewServeMux()

	mux.HandleFunc("/api/dashboard", h.HandleDashboard)
	mux.HandleFunc("/healthz", h.HandleHealth)

	var handler http.Handler = mux
	handler = CORSMiddleware(handler)
	handler = LoggingMiddleware(handler)
	handler = RecoveryMiddleware(handler)

	return handler
}
