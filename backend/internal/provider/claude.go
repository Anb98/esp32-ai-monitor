package provider

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/esp32-ai-monitor/backend/internal/model"
)

type claudeCredsPayload struct {
	SessionToken string `json:"session_token"`
	Token        string `json:"token"`
	ApiKey       string `json:"api_key"`
	Expired      bool   `json:"expired"`
	Quota5h      *struct {
		Used           float64 `json:"used"`
		Limit          float64 `json:"limit"`
		ResetTimestamp int64   `json:"reset_timestamp"`
	} `json:"quota_5h"`
	QuotaWeekly *struct {
		Used           float64 `json:"used"`
		Limit          float64 `json:"limit"`
		ResetTimestamp int64   `json:"reset_timestamp"`
	} `json:"quota_weekly"`
}

type ClaudeEngine struct {
	configDir string
	scraper   *StatusScraper
}

func NewClaudeEngine(configDir string, scraper *StatusScraper) *ClaudeEngine {
	if scraper == nil {
		scraper = NewStatusScraper()
	}
	return &ClaudeEngine{
		configDir: configDir,
		scraper:   scraper,
	}
}

func (e *ClaudeEngine) ID() string {
	return "claude"
}

func (e *ClaudeEngine) Name() string {
	return "Claude Code"
}

func (e *ClaudeEngine) FetchMetrics(ctx context.Context) (*model.Provider, error) {
	status := e.scraper.FetchClaudeStatus(ctx)

	p := &model.Provider{
		ID:              e.ID(),
		Name:            e.Name(),
		Status:          status,
		AuthValid:       false,
		ReLoginRequired: true,
		Metrics: model.ProviderMetrics{
			Quota5h: model.QuotaWindow{
				Used:           0,
				Limit:          100,
				Percentage:     0,
				ResetTime:      "00:00",
				ResetTimestamp: 0,
			},
			QuotaWeekly: model.QuotaWindow{
				Used:           0,
				Limit:          500,
				Percentage:     0,
				ResetTime:      "Dom 00:00",
				ResetTimestamp: 0,
			},
		},
	}

	credsData, err := findAndReadCreds(e.configDir, []string{
		"credentials.json",
		"session.json",
		"config.json",
		".claude.json",
	})
	if err != nil || len(credsData) == 0 {
		return p, nil
	}

	var creds claudeCredsPayload
	if err := json.Unmarshal(credsData, &creds); err != nil {
		return p, nil
	}

	token := creds.SessionToken
	if token == "" {
		token = creds.Token
	}
	if token == "" {
		token = creds.ApiKey
	}

	if token == "" || creds.Expired {
		return p, nil
	}

	p.AuthValid = true
	p.ReLoginRequired = false

	now := time.Now()

	// 5h Quota calculation
	var q5hUsed, q5hLimit float64 = 0.0, 100.0
	var q5hResetTs int64
	if creds.Quota5h != nil {
		q5hUsed = creds.Quota5h.Used
		if creds.Quota5h.Limit > 0 {
			q5hLimit = creds.Quota5h.Limit
		}
		q5hResetTs = creds.Quota5h.ResetTimestamp
	}

	if q5hResetTs == 0 {
		// Calculate default next 5-hour boundary
		q5hReset := now.Add(2 * time.Hour).Truncate(time.Minute)
		q5hResetTs = q5hReset.Unix()
	}
	q5hTime := time.Unix(q5hResetTs, 0).Local()
	q5hPct := 0.0
	if q5hLimit > 0 {
		q5hPct = (q5hUsed / q5hLimit) * 100.0
	}

	p.Metrics.Quota5h = model.QuotaWindow{
		Used:           q5hUsed,
		Limit:          q5hLimit,
		Percentage:     q5hPct,
		ResetTime:      q5hTime.Format("15:04"),
		ResetTimestamp: q5hResetTs,
	}

	// Weekly Quota calculation
	var qWkUsed, qWkLimit float64 = 0.0, 500.0
	var qWkResetTs int64
	if creds.QuotaWeekly != nil {
		qWkUsed = creds.QuotaWeekly.Used
		if creds.QuotaWeekly.Limit > 0 {
			qWkLimit = creds.QuotaWeekly.Limit
		}
		qWkResetTs = creds.QuotaWeekly.ResetTimestamp
	}

	if qWkResetTs == 0 {
		// Calculate next Sunday 00:00
		daysUntilSunday := (7 - int(now.Weekday())) % 7
		if daysUntilSunday == 0 {
			daysUntilSunday = 7
		}
		nextSun := time.Date(now.Year(), now.Month(), now.Day()+daysUntilSunday, 0, 0, 0, 0, now.Location())
		qWkResetTs = nextSun.Unix()
	}
	qWkTime := time.Unix(qWkResetTs, 0).Local()
	qWkPct := 0.0
	if qWkLimit > 0 {
		qWkPct = (qWkUsed / qWkLimit) * 100.0
	}

	p.Metrics.QuotaWeekly = model.QuotaWindow{
		Used:           qWkUsed,
		Limit:          qWkLimit,
		Percentage:     qWkPct,
		ResetTime:      formatWeeklyReset(qWkTime),
		ResetTimestamp: qWkResetTs,
	}

	return p, nil
}

func findAndReadCreds(baseDir string, filenames []string) ([]byte, error) {
	// If baseDir is directly a file
	fi, err := os.Stat(baseDir)
	if err == nil && !fi.IsDir() {
		return os.ReadFile(baseDir)
	}

	for _, name := range filenames {
		path := filepath.Join(baseDir, name)
		data, err := os.ReadFile(path)
		if err == nil && len(data) > 0 {
			return data, nil
		}
	}
	return nil, os.ErrNotExist
}

func formatWeeklyReset(t time.Time) string {
	days := map[time.Weekday]string{
		time.Sunday:    "Dom",
		time.Monday:    "Lun",
		time.Tuesday:   "Mar",
		time.Wednesday: "Mié",
		time.Thursday:  "Jue",
		time.Friday:    "Vie",
		time.Saturday:  "Sáb",
	}
	dayName := days[t.Weekday()]
	if dayName == "" {
		dayName = "Dom"
	}
	return fmt.Sprintf("%s %02d:%02d", dayName, t.Hour(), t.Minute())
}
