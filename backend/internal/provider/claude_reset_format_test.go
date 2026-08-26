package provider

import (
	"strings"
	"testing"
	"time"
)

// A bare "09:00" on the weekly window reads as a time that already passed
// today when the real reset is days out, so it must carry the date; the
// 5-hour window always lands within the day and stays clock-only.
func TestFormatResetTime_WeeklyCarriesDate_FiveHourStaysClockOnly(t *testing.T) {
	// Sunday, 30 August 2026 at 09:00 local.
	ts := time.Date(2026, time.August, 30, 9, 0, 0, 0, time.Local)

	if got, want := formatResetTime(ts, false), "9:00 AM"; got != want {
		t.Errorf("5h window: expected %q, got %q", want, got)
	}
	if got, want := formatResetTime(ts, true), "Dom 30 Ago 9:00 AM"; got != want {
		t.Errorf("weekly window: expected %q, got %q", want, got)
	}
}

// Month() is 1-based and the lookup table is 0-based, so an off-by-one here
// would silently label January as February for eleven months of the year.
func TestFormatResetTime_MonthAndWeekdayLookupsAreAligned(t *testing.T) {
	for i, want := range shortMonthsES {
		ts := time.Date(2026, time.Month(i+1), 1, 0, 0, 0, 0, time.Local)
		if got := formatResetTime(ts, true); !strings.Contains(got, want) {
			t.Errorf("month %d: expected %q inside %q", i+1, want, got)
		}
	}
	// 4 Jan 2026 is a Sunday, so seven consecutive days cover every weekday.
	for d := 0; d < 7; d++ {
		ts := time.Date(2026, time.January, 4+d, 0, 0, 0, 0, time.Local)
		want := shortWeekdaysES[ts.Weekday()]
		if got := formatResetTime(ts, true); !strings.HasPrefix(got, want) {
			t.Errorf("weekday %v: expected prefix %q, got %q", ts.Weekday(), want, got)
		}
	}
}
