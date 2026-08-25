package provider

import (
	"context"
	"encoding/json"
	"time"

	"github.com/esp32-ai-monitor/backend/internal/model"
)

type antigravityCredsPayload struct {
	AccessToken  string `json:"access_token"`
	Token        string `json:"token"`
	ApiKey       string `json:"api_key"`
	RefreshToken string `json:"refresh_token"`
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

type AntigravityEngine struct {
	configDir string
	scraper   *StatusScraper
}

func NewAntigravityEngine(configDir string, scraper *StatusScraper) *AntigravityEngine {
	if scraper == nil {
		scraper = NewStatusScraper()
	}
	return &AntigravityEngine{
		configDir: configDir,
		scraper:   scraper,
	}
}

func (e *AntigravityEngine) ID() string {
	return "antigravity"
}

func (e *AntigravityEngine) Name() string {
	return "Google Antigravity"
}

func (e *AntigravityEngine) FetchMetrics(ctx context.Context) (*model.Provider, error) {
	status := e.scraper.FetchGoogleStatus(ctx)

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
				Limit:          1000,
				Percentage:     0,
				ResetTime:      "Lun 00:00",
				ResetTimestamp: 0,
			},
		},
	}

	credsData, err := findAndReadCreds(e.configDir, []string{
		"credentials.json",
		"session.json",
		"config.json",
		".gemini.json",
	})
	if err != nil || len(credsData) == 0 {
		return p, nil
	}

	var creds antigravityCredsPayload
	if err := json.Unmarshal(credsData, &creds); err != nil {
		return p, nil
	}

	token := creds.AccessToken
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
		// Default next 5h boundary
		q5hReset := now.Add(3 * time.Hour).Truncate(time.Minute)
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
	var qWkUsed, qWkLimit float64 = 0.0, 1000.0
	var qWkResetTs int64
	if creds.QuotaWeekly != nil {
		qWkUsed = creds.QuotaWeekly.Used
		if creds.QuotaWeekly.Limit > 0 {
			qWkLimit = creds.QuotaWeekly.Limit
		}
		qWkResetTs = creds.QuotaWeekly.ResetTimestamp
	}

	if qWkResetTs == 0 {
		// Calculate next Monday 00:00
		daysUntilMonday := (8 - int(now.Weekday())) % 7
		if daysUntilMonday == 0 {
			daysUntilMonday = 7
		}
		nextMon := time.Date(now.Year(), now.Month(), now.Day()+daysUntilMonday, 0, 0, 0, 0, now.Location())
		qWkResetTs = nextMon.Unix()
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
