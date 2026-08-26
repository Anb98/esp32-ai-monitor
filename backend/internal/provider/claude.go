package provider

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/esp32-ai-monitor/backend/internal/model"
)

type ClaudeEngine struct {
	configDir string
	home      string
	scraper   *StatusScraper
	prober    *AuthProber
}

func NewClaudeEngine(configDir string, scraper *StatusScraper) *ClaudeEngine {
	home, _ := os.UserHomeDir()
	return NewClaudeEngineWithProber(configDir, home, scraper, nil)
}

// NewClaudeEngineWithProber allows injecting a custom AuthProber (e.g. a
// stub ProbeFunc in tests) instead of the real network probe wired by
// NewClaudeEngine.
func NewClaudeEngineWithProber(configDir, home string, scraper *StatusScraper, prober *AuthProber) *ClaudeEngine {
	if scraper == nil {
		scraper = NewStatusScraper()
	}
	if prober == nil {
		prober = NewAuthProber(nil, probeClaudeAuth)
	}
	return &ClaudeEngine{
		configDir: configDir,
		home:      home,
		scraper:   scraper,
		prober:    prober,
	}
}

func (e *ClaudeEngine) ID() string {
	return "claude"
}

func (e *ClaudeEngine) Name() string {
	return "Claude Code"
}

func (e *ClaudeEngine) FetchMetrics(ctx context.Context) (*model.Provider, error) {
	status, statusStale := e.scraper.FetchClaudeStatus(ctx)

	cred := ResolveClaudeCreds(e.configDir, e.home)
	credMtime := fileMtime(filepath.Join(e.configDir, ".credentials.json"))
	now := time.Now()

	authState, authStale, checkedAt := e.prober.Classify(ctx, cred, credMtime, now)

	if cred.State == CredAbsent && ClaudeJSONHasOAuthAccount(e.home) {
		authStale = true
	}

	p := &model.Provider{
		ID:            e.ID(),
		Name:          e.Name(),
		Status:        status,
		AuthCheckedAt: checkedAt,
		Stale:         statusStale || authStale,
	}
	p.ApplyAuthState(authState)

	quota := e.prober.LastQuota()
	p.Metrics.Quota5h = quotaWindow(quota.FiveHour, now, false)
	p.Metrics.QuotaWeekly = quotaWindow(quota.SevenDay, now, true)

	return p, nil
}

// The firmware UI is Spanish, and Go's time package only formats English day
// and month names, so they are spelled out here.
var (
	shortWeekdaysES = [...]string{"Dom", "Lun", "Mar", "Mie", "Jue", "Vie", "Sab"}
	shortMonthsES   = [...]string{"Ene", "Feb", "Mar", "Abr", "May", "Jun",
		"Jul", "Ago", "Sep", "Oct", "Nov", "Dic"}
)

// formatResetTime renders a reset instant for the display. The 5-hour window
// always lands within the day, so the clock alone is unambiguous; the weekly
// window can be days out, where a bare "09:00" reads as a time that already
// passed today, so it carries the date too.
func formatResetTime(t time.Time, withDate bool) string {
	clock := t.Format("3:04 PM")
	if !withDate {
		return clock
	}
	return fmt.Sprintf("%s %d %s %s",
		shortWeekdaysES[t.Weekday()],
		t.Day(),
		shortMonthsES[t.Month()-1],
		clock)
}

// quotaWindow projects one probed window onto the API model. Used and Limit
// stay 0 because /api/oauth/usage exposes only a utilization percentage on
// subscription plans, and a derived count would be a fabricated reading.
func quotaWindow(info QuotaWindowInfo, now time.Time, datedReset bool) model.QuotaWindow {
	if !info.Available {
		return model.QuotaWindow{Available: false}
	}

	w := model.QuotaWindow{Available: true, Percentage: info.Percentage}
	if !info.ResetsAt.IsZero() {
		w.ResetTimestamp = info.ResetsAt.Unix()
		if secs := int64(info.ResetsAt.Sub(now).Seconds()); secs > 0 {
			w.ResetInSeconds = secs
		}
		w.ResetTime = formatResetTime(info.ResetsAt.Local(), datedReset)
	}
	return w
}
