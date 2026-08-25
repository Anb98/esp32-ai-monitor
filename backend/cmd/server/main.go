package main

import (
	"context"
	"errors"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/esp32-ai-monitor/backend/internal/api"
	"github.com/esp32-ai-monitor/backend/internal/cache"
	"github.com/esp32-ai-monitor/backend/internal/config"
	"github.com/esp32-ai-monitor/backend/internal/provider"
)

func main() {
	log.Println("[INFO] Starting esp32-ai-monitor backend service...")

	cfg := config.LoadConfig()
	log.Printf("[INFO] Config loaded - Port: %s, PollInterval: %v, ClaudeDir: %s, GeminiDir: %s",
		cfg.Port, cfg.PollInterval, cfg.ClaudeConfigDir, cfg.GeminiConfigDir)

	c := cache.New()
	scraper := provider.NewStatusScraper()

	claudeEngine := provider.NewClaudeEngine(cfg.ClaudeConfigDir, scraper)
	antigravityEngine := provider.NewAntigravityEngine(cfg.GeminiConfigDir, scraper)

	engines := []provider.ProviderEngine{claudeEngine, antigravityEngine}
	watcher := provider.NewTokenWatcher(engines, cfg.PollInterval)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	// Start background watcher loop
	go watcher.Start(ctx, func() {
		dash, err := watcher.PollOnce(ctx)
		if err != nil {
			log.Printf("[WARN] Error during poll: %v", err)
			return
		}
		c.Set(dash)
		log.Printf("[DEBUG] Dashboard cache updated: %d providers, memory: %.2fMB",
			len(dash.Providers), dash.System.MemoryMB)
	})

	handler := api.NewHandler(c)
	router := api.NewRouter(handler)

	server := &http.Server{
		Addr:         ":" + cfg.Port,
		Handler:      router,
		ReadTimeout:  5 * time.Second,
		WriteTimeout: 5 * time.Second,
		IdleTimeout:  30 * time.Second,
	}

	// Server startup
	go func() {
		log.Printf("[INFO] HTTP server listening on :%s", cfg.Port)
		if err := server.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
			log.Fatalf("[FATAL] HTTP server error: %v", err)
		}
	}()

	// Graceful shutdown handling
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)
	<-sigChan

	log.Println("[INFO] Shutting down backend service...")
	cancel()

	shutdownCtx, shutdownCancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer shutdownCancel()

	if err := server.Shutdown(shutdownCtx); err != nil {
		log.Printf("[ERROR] Graceful shutdown failed: %v", err)
	} else {
		log.Println("[INFO] Backend service stopped gracefully.")
	}
}
