#include "ui_manager.h"
#include "esp_log.h"

static const char *TAG = "UIManager";

UIManager ui;

// Renders totalSeconds as "HH:MM:SS", or "Nd HH:MM" once it spans a day or
// more (a 5h/weekly reset countdown can legitimately reach several days).
static void formatCountdown(int32_t totalSeconds, char *buf, size_t bufLen) {
    if (totalSeconds < 0) totalSeconds = 0;
    int32_t days = totalSeconds / 86400;
    int32_t rem = totalSeconds % 86400;
    int32_t hours = rem / 3600;
    int32_t minutes = (rem % 3600) / 60;
    int32_t seconds = rem % 60;
    if (days > 0) {
        snprintf(buf, bufLen, "%ldd %02ld:%02ld", (long)days, (long)hours, (long)minutes);
    } else {
        snprintf(buf, bufLen, "%02ld:%02ld:%02ld", (long)hours, (long)minutes, (long)seconds);
    }
}

// The backend leaves reset_time empty when upstream published the window with no
// resets_at, and a bare "Reset: " prefix reads as a broken label rather than as
// a missing clock.
static void setResetLabel(lv_obj_t *label, const String &resetTime) {
    if (resetTime.isEmpty()) {
        lv_label_set_text(label, "");
        return;
    }
    lv_label_set_text_fmt(label, "Reset: %s", resetTime.c_str());
}

UIManager::UIManager()
    : _lock(nullptr),
      _cardClaude(nullptr),
      _countdownClaude{0, 0},
      _countdownWeeklyClaude{0, 0},
      _pillClaude(nullptr),
      _pillLabelClaude(nullptr),
      _staleBadgeClaude(nullptr),
      _bar5hClaude(nullptr),
      _label5hPctClaude(nullptr),
      _label5hResetClaude(nullptr),
      _barWkClaude(nullptr),
      _labelWkPctClaude(nullptr),
      _labelWkResetClaude(nullptr),
      _overlayClaude(nullptr),
      _countdownValueClaude(nullptr),
      _countdownValueWeeklyClaude(nullptr) {}

void UIManager::createLock() {
    _lock = xSemaphoreCreateMutex();
}

bool UIManager::uiLock() {
    if (!_lock) return false;
    return xSemaphoreTake(_lock, pdMS_TO_TICKS(100)) == pdTRUE;
}

void UIManager::uiUnlock() {
    if (_lock) {
        xSemaphoreGive(_lock);
    }
}

void UIManager::init() {
    ESP_LOGI(TAG, "Initializing LVGL UI Manager...");

    // lv_init() runs in setup() before the display driver registers itself,
    // so by the time we get here LVGL is already up.

    // Dark background for root screen
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0F172A), LV_PART_MAIN); // Dark Slate
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    createScreenViews();
    lv_timer_create(countdownTimerCb, 1000, nullptr);

    ESP_LOGI(TAG, "LVGL UI Manager initialized.");
}

void UIManager::createScreenViews() {
    lv_obj_t *scr = lv_scr_act();

    _cardClaude = lv_obj_create(scr);
    lv_obj_set_size(_cardClaude, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_pos(_cardClaude, 0, 0);
    lv_obj_set_style_bg_opa(_cardClaude, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(_cardClaude, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_cardClaude, 0, LV_PART_MAIN);
    lv_obj_clear_flag(_cardClaude, LV_OBJ_FLAG_SCROLLABLE);

    buildProviderScreen("Claude Code", &_cardClaude,
                        &_pillClaude, &_pillLabelClaude, &_staleBadgeClaude,
                        &_bar5hClaude, &_label5hPctClaude, &_label5hResetClaude,
                        &_barWkClaude, &_labelWkPctClaude, &_labelWkResetClaude,
                        &_overlayClaude,
                        &_countdownValueClaude, &_countdownValueWeeklyClaude);
}

void UIManager::buildProviderScreen(const char *title, lv_obj_t **screenObj,
                                    lv_obj_t **pillObj, lv_obj_t **pillLabelObj, lv_obj_t **staleBadgeObj,
                                    lv_obj_t **bar5hObj, lv_obj_t **label5hPctObj, lv_obj_t **label5hResetObj,
                                    lv_obj_t **barWkObj, lv_obj_t **labelWkPctObj, lv_obj_t **labelWkResetObj,
                                    lv_obj_t **overlayObj,
                                    lv_obj_t **countdownValueObj, lv_obj_t **countdownValueWeeklyObj) {
    lv_obj_t *parent = *screenObj;
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    // Title Label
    lv_obj_t *lblTitle = lv_label_create(parent);
    lv_label_set_text(lblTitle, title);
    lv_obj_set_style_text_color(lblTitle, lv_color_hex(0xF8FAFC), LV_PART_MAIN);
    lv_obj_set_style_text_font(lblTitle, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(lblTitle, LV_ALIGN_TOP_LEFT, 36, 28);

    // Status Pill
    *pillObj = lv_obj_create(parent);
    lv_obj_set_size(*pillObj, 130, 28);
    lv_obj_align(*pillObj, LV_ALIGN_TOP_RIGHT, -36, 26);
    lv_obj_set_style_radius(*pillObj, 14, LV_PART_MAIN);
    lv_obj_set_style_border_width(*pillObj, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(*pillObj, lv_color_hex(0x065F46), LV_PART_MAIN); // Green base
    lv_obj_clear_flag(*pillObj, LV_OBJ_FLAG_SCROLLABLE);

    *pillLabelObj = lv_label_create(*pillObj);
    lv_label_set_text(*pillLabelObj, "Operativo");
    lv_obj_set_style_text_color(*pillLabelObj, lv_color_hex(0xD1FAE5), LV_PART_MAIN);
    lv_obj_set_style_text_font(*pillLabelObj, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(*pillLabelObj);

    // Stale/disconnected badge, hidden while the payload is fresh. Bottom-
    // centred rather than stacked under the status pill: at TOP_RIGHT it
    // overlapped the 5h percentage, and stale is orthogonal to status, so both
    // must stay legible at once. It also sits below the re-login overlay,
    // which ends at y=390, so a modal never hides the connection warning.
    *staleBadgeObj = lv_obj_create(parent);
    lv_obj_set_size(*staleBadgeObj, 130, 24);
    lv_obj_align(*staleBadgeObj, LV_ALIGN_TOP_MID, 0, 430);
    lv_obj_set_style_radius(*staleBadgeObj, 12, LV_PART_MAIN);
    lv_obj_set_style_border_width(*staleBadgeObj, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(*staleBadgeObj, lv_color_hex(0x475569), LV_PART_MAIN);
    lv_obj_clear_flag(*staleBadgeObj, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lblStale = lv_label_create(*staleBadgeObj);
    lv_label_set_text(lblStale, "SIN CONEXION");
    lv_obj_set_style_text_color(lblStale, lv_color_hex(0xFBBF24), LV_PART_MAIN);
    lv_obj_set_style_text_font(lblStale, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(lblStale);
    lv_obj_add_flag(*staleBadgeObj, LV_OBJ_FLAG_HIDDEN);

    // 5-Hour Quota Section
    lv_obj_t *lbl5hHeader = lv_label_create(parent);
    lv_label_set_text(lbl5hHeader, "Cuota 5 Horas");
    lv_obj_set_style_text_color(lbl5hHeader, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl5hHeader, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(lbl5hHeader, LV_ALIGN_TOP_LEFT, 36, 78);

    *label5hPctObj = lv_label_create(parent);
    lv_label_set_text(*label5hPctObj, "Sin datos");
    lv_obj_set_style_text_color(*label5hPctObj, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_set_style_text_font(*label5hPctObj, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(*label5hPctObj, LV_ALIGN_TOP_RIGHT, -36, 76);

    *bar5hObj = lv_bar_create(parent);
    lv_obj_set_size(*bar5hObj, 408, 18);
    lv_obj_align(*bar5hObj, LV_ALIGN_TOP_MID, 0, 112);
    lv_obj_set_style_bg_color(*bar5hObj, lv_color_hex(0x1E293B), LV_PART_MAIN);
    lv_obj_set_style_bg_color(*bar5hObj, lv_color_hex(0x10B981), LV_PART_INDICATOR);
    lv_obj_set_style_radius(*bar5hObj, 9, LV_PART_MAIN);
    lv_obj_set_style_radius(*bar5hObj, 9, LV_PART_INDICATOR);
    lv_bar_set_value(*bar5hObj, 0, LV_ANIM_ON);

    *label5hResetObj = lv_label_create(parent);
    lv_label_set_text(*label5hResetObj, "");
    lv_obj_set_style_text_color(*label5hResetObj, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_set_style_text_font(*label5hResetObj, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(*label5hResetObj, LV_ALIGN_TOP_LEFT, 36, 136);

    // Weekly Quota Section
    lv_obj_t *lblWkHeader = lv_label_create(parent);
    lv_label_set_text(lblWkHeader, "Cuota Semanal");
    lv_obj_set_style_text_color(lblWkHeader, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(lblWkHeader, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(lblWkHeader, LV_ALIGN_TOP_LEFT, 36, 170);

    *labelWkPctObj = lv_label_create(parent);
    lv_label_set_text(*labelWkPctObj, "Sin datos");
    lv_obj_set_style_text_color(*labelWkPctObj, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_set_style_text_font(*labelWkPctObj, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(*labelWkPctObj, LV_ALIGN_TOP_RIGHT, -36, 168);

    *barWkObj = lv_bar_create(parent);
    lv_obj_set_size(*barWkObj, 408, 18);
    lv_obj_align(*barWkObj, LV_ALIGN_TOP_MID, 0, 204);
    lv_obj_set_style_bg_color(*barWkObj, lv_color_hex(0x1E293B), LV_PART_MAIN);
    lv_obj_set_style_bg_color(*barWkObj, lv_color_hex(0x10B981), LV_PART_INDICATOR);
    lv_obj_set_style_radius(*barWkObj, 9, LV_PART_MAIN);
    lv_obj_set_style_radius(*barWkObj, 9, LV_PART_INDICATOR);
    lv_bar_set_value(*barWkObj, 0, LV_ANIM_ON);

    *labelWkResetObj = lv_label_create(parent);
    lv_label_set_text(*labelWkResetObj, "");
    lv_obj_set_style_text_color(*labelWkResetObj, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_set_style_text_font(*labelWkResetObj, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(*labelWkResetObj, LV_ALIGN_TOP_LEFT, 36, 228);

    // Countdown container: transparent, sole occupant of the lower half
    lv_obj_t *countdownCont = lv_obj_create(parent);
    lv_obj_set_size(countdownCont, 480, 206);
    lv_obj_set_pos(countdownCont, 0, 274);
    lv_obj_set_style_bg_opa(countdownCont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(countdownCont, 0, LV_PART_MAIN);
    // lv_obj_create inherits the theme's card padding; zero it so the child
    // offsets below are absolute within the container, not pad-relative.
    lv_obj_set_style_pad_all(countdownCont, 0, LV_PART_MAIN);
    lv_obj_clear_flag(countdownCont, LV_OBJ_FLAG_SCROLLABLE);

    // Two stacked countdowns rather than two equal columns: the 5h window is
    // the one that actually blocks work, so it stays the 48px hero and the
    // weekly window reads as 28px context below it. A 4px caption-to-value gap
    // binds each pair; the 16px gap between pairs separates them. Both stay
    // above y=165 so the re-login overlay covers them completely.
    lv_obj_t *countdownCaptionObj = lv_label_create(countdownCont);
    lv_label_set_text(countdownCaptionObj, "RESET 5H");
    lv_obj_set_style_text_color(countdownCaptionObj, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(countdownCaptionObj, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(countdownCaptionObj, LV_ALIGN_TOP_MID, 0, 4);

    *countdownValueObj = lv_label_create(countdownCont);
    lv_label_set_text(*countdownValueObj, "--:--:--");
    lv_obj_set_style_text_color(*countdownValueObj, lv_color_hex(0xF8FAFC), LV_PART_MAIN);
    lv_obj_set_style_text_font(*countdownValueObj, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_align(*countdownValueObj, LV_ALIGN_TOP_MID, 0, 26);

    lv_obj_t *countdownCaptionWeeklyObj = lv_label_create(countdownCont);
    lv_label_set_text(countdownCaptionWeeklyObj, "RESET SEMANAL");
    lv_obj_set_style_text_color(countdownCaptionWeeklyObj, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(countdownCaptionWeeklyObj, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(countdownCaptionWeeklyObj, LV_ALIGN_TOP_MID, 0, 92);

    *countdownValueWeeklyObj = lv_label_create(countdownCont);
    lv_label_set_text(*countdownValueWeeklyObj, "--:--:--");
    lv_obj_set_style_text_color(*countdownValueWeeklyObj, lv_color_hex(0xF8FAFC), LV_PART_MAIN);
    lv_obj_set_style_text_font(*countdownValueWeeklyObj, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_align(*countdownValueWeeklyObj, LV_ALIGN_TOP_MID, 0, 114);

    // Warning Overlay Card (Modal for Re-login required); the only widget
    // allowed to cover the lower half besides the countdown container.
    *overlayObj = lv_obj_create(parent);
    lv_obj_set_size(*overlayObj, 408, 220);
    lv_obj_align(*overlayObj, LV_ALIGN_CENTER, 0, 40);
    lv_obj_set_style_bg_color(*overlayObj, lv_color_hex(0x7F1D1D), LV_PART_MAIN); // Red background
    lv_obj_set_style_radius(*overlayObj, 16, LV_PART_MAIN);
    lv_obj_set_style_border_color(*overlayObj, lv_color_hex(0xEF4444), LV_PART_MAIN);
    lv_obj_set_style_border_width(*overlayObj, 2, LV_PART_MAIN);
    lv_obj_clear_flag(*overlayObj, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lblWarnIcon = lv_label_create(*overlayObj);
    lv_label_set_text(lblWarnIcon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(lblWarnIcon, lv_color_hex(0xFCA5A5), LV_PART_MAIN);
    lv_obj_set_style_text_font(lblWarnIcon, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_align(lblWarnIcon, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *lblWarnTitle = lv_label_create(*overlayObj);
    lv_label_set_text(lblWarnTitle, "RE-LOGIN REQUERIDO");
    lv_obj_set_style_text_color(lblWarnTitle, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(lblWarnTitle, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(lblWarnTitle, LV_ALIGN_TOP_MID, 0, 50);

    lv_obj_t *lblWarnSub = lv_label_create(*overlayObj);
    lv_label_set_text(lblWarnSub, "Ejecuta 'claude' en terminal");
    lv_obj_set_style_text_color(lblWarnSub, lv_color_hex(0xFECACA), LV_PART_MAIN);
    lv_obj_set_style_text_font(lblWarnSub, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(lblWarnSub, LV_ALIGN_BOTTOM_MID, 0, -25);

    // Hide overlay initially
    lv_obj_add_flag(*overlayObj, LV_OBJ_FLAG_HIDDEN);
}

lv_color_t UIManager::getQuotaBarColor(float pct) {
    if (pct < 70.0f) {
        return lv_color_hex(0x10B981); // Emerald Green
    } else if (pct < 90.0f) {
        return lv_color_hex(0xF59E0B); // Amber Yellow
    } else {
        return lv_color_hex(0xEF4444); // Red
    }
}

void UIManager::updateScreenWidgets(const ProviderUIData &data,
                                    lv_obj_t *pillObj, lv_obj_t *pillLabelObj, lv_obj_t *staleBadgeObj,
                                    lv_obj_t *bar5hObj, lv_obj_t *label5hPctObj, lv_obj_t *label5hResetObj,
                                    lv_obj_t *barWkObj, lv_obj_t *labelWkPctObj, lv_obj_t *labelWkResetObj,
                                    lv_obj_t *overlayObj) {
    // Update Status Pill
    if (data.status == "operational") {
        lv_obj_set_style_bg_color(pillObj, lv_color_hex(0x065F46), LV_PART_MAIN);
        lv_label_set_text(pillLabelObj, "Operativo");
        lv_obj_set_style_text_color(pillLabelObj, lv_color_hex(0xD1FAE5), LV_PART_MAIN);
    } else if (data.status == "degraded") {
        lv_obj_set_style_bg_color(pillObj, lv_color_hex(0x78350F), LV_PART_MAIN);
        lv_label_set_text(pillLabelObj, "Degradado");
        lv_obj_set_style_text_color(pillLabelObj, lv_color_hex(0xFEF3C7), LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_color(pillObj, lv_color_hex(0x7F1D1D), LV_PART_MAIN);
        lv_label_set_text(pillLabelObj, "Interrupcion");
        lv_obj_set_style_text_color(pillLabelObj, lv_color_hex(0xFEE2E2), LV_PART_MAIN);
    }

    // Stale/disconnected badge (label text is fixed at creation time)
    if (data.stale) {
        lv_obj_clear_flag(staleBadgeObj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(staleBadgeObj, LV_OBJ_FLAG_HIDDEN);
    }

    // 5h Quota Bar & Labels
    if (data.quota5hAvailable) {
        // Kept as snprintf + lv_label_set_text, not lv_label_set_text_fmt:
        // lv_conf.h leaves LV_SPRINTF_USE_FLOAT unset (defaults to 0), so
        // LVGL's built-in lv_vsnprintf drops %f entirely. Converting this site
        // needs that flag flipped and a flash-time visual check first.
        char buf5h[32];
        snprintf(buf5h, sizeof(buf5h), "%.1f%%", data.quota5hPct);
        lv_label_set_text(label5hPctObj, buf5h);
        lv_obj_set_style_text_color(label5hPctObj, lv_color_hex(0xF8FAFC), LV_PART_MAIN);

        setResetLabel(label5hResetObj, data.quota5hResetTime);

        lv_bar_set_value(bar5hObj, (int32_t)data.quota5hPct, LV_ANIM_ON);
        lv_obj_set_style_bg_color(bar5hObj, getQuotaBarColor(data.quota5hPct), LV_PART_INDICATOR);
    } else {
        lv_label_set_text(label5hPctObj, "Sin datos");
        lv_obj_set_style_text_color(label5hPctObj, lv_color_hex(0x64748B), LV_PART_MAIN);
        lv_label_set_text(label5hResetObj, "");
        lv_bar_set_value(bar5hObj, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar5hObj, lv_color_hex(0x334155), LV_PART_INDICATOR);
    }

    // Weekly Quota Bar & Labels
    if (data.quotaWeeklyAvailable) {
        // Same LV_SPRINTF_USE_FLOAT constraint as the 5h site above.
        char bufWk[32];
        snprintf(bufWk, sizeof(bufWk), "%.1f%%", data.quotaWeeklyPct);
        lv_label_set_text(labelWkPctObj, bufWk);
        lv_obj_set_style_text_color(labelWkPctObj, lv_color_hex(0xF8FAFC), LV_PART_MAIN);

        setResetLabel(labelWkResetObj, data.quotaWeeklyResetTime);

        lv_bar_set_value(barWkObj, (int32_t)data.quotaWeeklyPct, LV_ANIM_ON);
        lv_obj_set_style_bg_color(barWkObj, getQuotaBarColor(data.quotaWeeklyPct), LV_PART_INDICATOR);
    } else {
        lv_label_set_text(labelWkPctObj, "Sin datos");
        lv_obj_set_style_text_color(labelWkPctObj, lv_color_hex(0x64748B), LV_PART_MAIN);
        lv_label_set_text(labelWkResetObj, "");
        lv_bar_set_value(barWkObj, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(barWkObj, lv_color_hex(0x334155), LV_PART_INDICATOR);
    }

    // re_login_required is the sole overlay trigger; a transient auth-probe
    // failure (auth_state == "unknown") must never raise it.
    if (data.reLoginRequired) {
        lv_obj_clear_flag(overlayObj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(overlayObj, LV_OBJ_FLAG_HIDDEN);
    }

    // One timestamp for both windows so they never drift by a tick.
    uint32_t seedAtMs = millis();

    // seedSeconds == 0 means this window has no live reset clock. A zero
    // resetIn is absence, not a countdown that already reached zero.
    _countdownClaude.seedSeconds =
        (data.quota5hAvailable && data.quota5hResetIn > 0) ? data.quota5hResetIn : 0;
    _countdownClaude.seedAtMs = seedAtMs;

    _countdownWeeklyClaude.seedSeconds =
        (data.quotaWeeklyAvailable && data.quotaWeeklyResetIn > 0) ? data.quotaWeeklyResetIn : 0;
    _countdownWeeklyClaude.seedAtMs = seedAtMs;

    tickCountdown();
}

void UIManager::updateClaude(const ProviderUIData &data) {
    updateScreenWidgets(data, _pillClaude, _pillLabelClaude, _staleBadgeClaude,
                        _bar5hClaude, _label5hPctClaude, _label5hResetClaude,
                        _barWkClaude, _labelWkPctClaude, _labelWkResetClaude,
                        _overlayClaude);
}

void UIManager::tickCountdown() {
    tickOneCountdown(_countdownClaude, _countdownValueClaude, _label5hResetClaude);
    tickOneCountdown(_countdownWeeklyClaude, _countdownValueWeeklyClaude, _labelWkResetClaude);
}

void UIManager::tickOneCountdown(CountdownState &state, lv_obj_t *valueObj, lv_obj_t *resetLabelObj) {
    if (!valueObj) return;

    // millis() wraps every ~49 days; unsigned subtraction stays correct across
    // that wrap since both operands are uint32_t. A seed older than the interval
    // it described has expired: no poll has landed since, so it is stale, not a
    // countdown that legitimately reached zero.
    uint32_t elapsedSec = (millis() - state.seedAtMs) / 1000;
    bool live = state.seedSeconds > 0 && elapsedSec < (uint32_t)state.seedSeconds;
    int32_t remaining = live ? state.seedSeconds - (int32_t)elapsedSec : 0;

    // A non-positive remainder is the absence of a reset clock, not a countdown
    // that legitimately hit zero: either the backend published the window with
    // no resets_at (a fresh 5h window with no usage reports exactly that), or
    // the seed expired because no poll has landed since. Both must read as no
    // data instead of a bright 00:00:00 that passes for a live reading. The
    // caption is a section title, so it stays put and only the value drops out.
    if (remaining <= 0) {
        lv_label_set_text(valueObj, "--:--:--");
        lv_obj_set_style_text_color(valueObj, lv_color_hex(0x475569), LV_PART_MAIN);
        // The reset clock and the countdown answer the same question, so they
        // share one criterion. Without this the label keeps a bright
        // "Reset: 8:00 PM" beside a dim "--:--:--" whenever the backend clamps
        // reset_in_seconds to 0 while still publishing reset_time.
        if (resetLabelObj) lv_label_set_text(resetLabelObj, "");
        return;
    }

    char buf[16];
    formatCountdown(remaining, buf, sizeof(buf));
    lv_label_set_text(valueObj, buf);
    lv_obj_set_style_text_color(valueObj, lv_color_hex(0xF8FAFC), LV_PART_MAIN);
}

void UIManager::countdownTimerCb(lv_timer_t *timer) {
    ui.tickCountdown();
}
