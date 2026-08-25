#include "ui_manager.h"
#include "esp_log.h"

static const char *TAG = "UIManager";

UIManager ui;

static void tileview_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *tv = lv_event_get_target(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t *act = lv_tileview_get_tile_act(tv);
        // Determine active screen index
        lv_coord_t x = lv_obj_get_x(act);
        uint8_t screenIdx = (x > 0) ? 1 : 0;
        ui.showScreen(screenIdx);
    }
}

UIManager::UIManager()
    : _activeScreen(0),
      _screenTileview(nullptr),
      _tileClaude(nullptr),
      _tileAntigravity(nullptr),
      _pillClaude(nullptr),
      _pillLabelClaude(nullptr),
      _bar5hClaude(nullptr),
      _label5hPctClaude(nullptr),
      _label5hResetClaude(nullptr),
      _barWkClaude(nullptr),
      _labelWkPctClaude(nullptr),
      _labelWkResetClaude(nullptr),
      _overlayClaude(nullptr),
      _pillAntigravity(nullptr),
      _pillLabelAntigravity(nullptr),
      _bar5hAntigravity(nullptr),
      _label5hPctAntigravity(nullptr),
      _label5hResetAntigravity(nullptr),
      _barWkAntigravity(nullptr),
      _labelWkPctAntigravity(nullptr),
      _labelWkResetAntigravity(nullptr),
      _overlayAntigravity(nullptr),
      _dot1(nullptr),
      _dot2(nullptr) {}

void UIManager::init() {
    ESP_LOGI(TAG, "Initializing LVGL UI Manager...");

    lv_init();

    // Dark background for root screen
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0F172A), LV_PART_MAIN); // Dark Slate
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    createScreenViews();
    updateIndicatorDots();

    ESP_LOGI(TAG, "LVGL UI Manager initialized.");
}

void UIManager::createScreenViews() {
    lv_obj_t *scr = lv_scr_act();

    _screenTileview = lv_tileview_create(scr);
    lv_obj_set_size(_screenTileview, LCD_WIDTH, LCD_HEIGHT - 30);
    lv_obj_set_pos(_screenTileview, 0, 0);
    lv_obj_set_style_bg_opa(_screenTileview, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_event_cb(_screenTileview, tileview_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    _tileClaude = lv_tileview_add_tile(_screenTileview, 0, 0, LV_DIR_HOR);
    _tileAntigravity = lv_tileview_add_tile(_screenTileview, 1, 0, LV_DIR_HOR);

    buildProviderScreen(0, "Claude Code", &_tileClaude,
                        &_pillClaude, &_pillLabelClaude,
                        &_bar5hClaude, &_label5hPctClaude, &_label5hResetClaude,
                        &_barWkClaude, &_labelWkPctClaude, &_labelWkResetClaude,
                        &_overlayClaude);

    buildProviderScreen(1, "Google Antigravity", &_tileAntigravity,
                        &_pillAntigravity, &_pillLabelAntigravity,
                        &_bar5hAntigravity, &_label5hPctAntigravity, &_label5hResetAntigravity,
                        &_barWkAntigravity, &_labelWkPctAntigravity, &_labelWkResetAntigravity,
                        &_overlayAntigravity);

    // Indicator dots container at bottom
    lv_obj_t *dotsCont = lv_obj_create(scr);
    lv_obj_set_size(dotsCont, 80, 24);
    lv_obj_align(dotsCont, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_opa(dotsCont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(dotsCont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dotsCont, 0, LV_PART_MAIN);
    lv_obj_clear_flag(dotsCont, LV_OBJ_FLAG_SCROLLABLE);

    _dot1 = lv_obj_create(dotsCont);
    lv_obj_set_size(_dot1, 10, 10);
    lv_obj_set_pos(_dot1, 24, 7);
    lv_obj_set_style_radius(_dot1, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(_dot1, 0, LV_PART_MAIN);

    _dot2 = lv_obj_create(dotsCont);
    lv_obj_set_size(_dot2, 10, 10);
    lv_obj_set_pos(_dot2, 46, 7);
    lv_obj_set_style_radius(_dot2, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(_dot2, 0, LV_PART_MAIN);
}

void UIManager::buildProviderScreen(uint8_t screenIdx, const char *title, lv_obj_t **screenObj,
                                    lv_obj_t **pillObj, lv_obj_t **pillLabelObj,
                                    lv_obj_t **bar5hObj, lv_obj_t **label5hPctObj, lv_obj_t **label5hResetObj,
                                    lv_obj_t **barWkObj, lv_obj_t **labelWkPctObj, lv_obj_t **labelWkResetObj,
                                    lv_obj_t **overlayObj) {
    lv_obj_t *parent = *screenObj;
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    // Title Label
    lv_obj_t *lblTitle = lv_label_create(parent);
    lv_label_set_text(lblTitle, title);
    lv_obj_set_style_text_color(lblTitle, lv_color_hex(0xF8FAFC), LV_PART_MAIN);
    lv_obj_set_style_text_font(lblTitle, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(lblTitle, LV_ALIGN_TOP_LEFT, 28, 28);

    // Status Pill
    *pillObj = lv_obj_create(parent);
    lv_obj_set_size(*pillObj, 130, 32);
    lv_obj_align(*pillObj, LV_ALIGN_TOP_RIGHT, -28, 26);
    lv_obj_set_style_radius(*pillObj, 16, LV_PART_MAIN);
    lv_obj_set_style_border_width(*pillObj, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(*pillObj, lv_color_hex(0x065F46), LV_PART_MAIN); // Green base
    lv_obj_clear_flag(*pillObj, LV_OBJ_FLAG_SCROLLABLE);

    *pillLabelObj = lv_label_create(*pillObj);
    lv_label_set_text(*pillLabelObj, "Operativo");
    lv_obj_set_style_text_color(*pillLabelObj, lv_color_hex(0xD1FAE5), LV_PART_MAIN);
    lv_obj_set_style_text_font(*pillLabelObj, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(*pillLabelObj);

    // 5-Hour Quota Section
    lv_obj_t *lbl5hHeader = lv_label_create(parent);
    lv_label_set_text(lbl5hHeader, "Cuota 5 Horas");
    lv_obj_set_style_text_color(lbl5hHeader, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl5hHeader, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(lbl5hHeader, LV_ALIGN_TOP_LEFT, 28, 90);

    *label5hPctObj = lv_label_create(parent);
    lv_label_set_text(*label5hPctObj, "0.0%");
    lv_obj_set_style_text_color(*label5hPctObj, lv_color_hex(0xF8FAFC), LV_PART_MAIN);
    lv_obj_set_style_text_font(*label5hPctObj, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(*label5hPctObj, LV_ALIGN_TOP_RIGHT, -28, 88);

    *bar5hObj = lv_bar_create(parent);
    lv_obj_set_size(*bar5hObj, 424, 22);
    lv_obj_align(*bar5hObj, LV_ALIGN_TOP_MID, 0, 122);
    lv_obj_set_style_bg_color(*bar5hObj, lv_color_hex(0x1E293B), LV_PART_MAIN);
    lv_obj_set_style_bg_color(*bar5hObj, lv_color_hex(0x10B981), LV_PART_INDICATOR);
    lv_obj_set_style_radius(*bar5hObj, 11, LV_PART_MAIN);
    lv_obj_set_style_radius(*bar5hObj, 11, LV_PART_INDICATOR);
    lv_bar_set_value(*bar5hObj, 0, LV_ANIM_ON);

    *label5hResetObj = lv_label_create(parent);
    lv_label_set_text(*label5hResetObj, "Reset: 00:00");
    lv_obj_set_style_text_color(*label5hResetObj, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_set_style_text_font(*label5hResetObj, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(*label5hResetObj, LV_ALIGN_TOP_LEFT, 28, 152);

    // Weekly Quota Section
    lv_obj_t *lblWkHeader = lv_label_create(parent);
    lv_label_set_text(lblWkHeader, "Cuota Semanal");
    lv_obj_set_style_text_color(lblWkHeader, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(lblWkHeader, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(lblWkHeader, LV_ALIGN_TOP_LEFT, 28, 205);

    *labelWkPctObj = lv_label_create(parent);
    lv_label_set_text(*labelWkPctObj, "0.0%");
    lv_obj_set_style_text_color(*labelWkPctObj, lv_color_hex(0xF8FAFC), LV_PART_MAIN);
    lv_obj_set_style_text_font(*labelWkPctObj, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(*labelWkPctObj, LV_ALIGN_TOP_RIGHT, -28, 203);

    *barWkObj = lv_bar_create(parent);
    lv_obj_set_size(*barWkObj, 424, 22);
    lv_obj_align(*barWkObj, LV_ALIGN_TOP_MID, 0, 237);
    lv_obj_set_style_bg_color(*barWkObj, lv_color_hex(0x1E293B), LV_PART_MAIN);
    lv_obj_set_style_bg_color(*barWkObj, lv_color_hex(0x10B981), LV_PART_INDICATOR);
    lv_obj_set_style_radius(*barWkObj, 11, LV_PART_MAIN);
    lv_obj_set_style_radius(*barWkObj, 11, LV_PART_INDICATOR);
    lv_bar_set_value(*barWkObj, 0, LV_ANIM_ON);

    *labelWkResetObj = lv_label_create(parent);
    lv_label_set_text(*labelWkResetObj, "Reset: Dom 00:00");
    lv_obj_set_style_text_color(*labelWkResetObj, lv_color_hex(0x64748B), LV_PART_MAIN);
    lv_obj_set_style_text_font(*labelWkResetObj, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(*labelWkResetObj, LV_ALIGN_TOP_LEFT, 28, 267);

    // Warning Overlay Card (Modal for Re-login required)
    *overlayObj = lv_obj_create(parent);
    lv_obj_set_size(*overlayObj, 424, 220);
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
    if (screenIdx == 0) {
        lv_label_set_text(lblWarnSub, "Ejecuta 'claude' en terminal");
    } else {
        lv_label_set_text(lblWarnSub, "Ejecuta 'agy login' en terminal");
    }
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
                                    lv_obj_t *pillObj, lv_obj_t *pillLabelObj,
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

    // Update 5h Quota Bar & Labels
    char buf5h[32];
    snprintf(buf5h, sizeof(buf5h), "%.1f%%", data.quota5hPct);
    lv_label_set_text(label5hPctObj, buf5h);

    char buf5hReset[48];
    snprintf(buf5hReset, sizeof(buf5hReset), "Reset: %s", data.quota5hResetTime.c_str());
    lv_label_set_text(label5hResetObj, buf5hReset);

    lv_bar_set_value(bar5hObj, (int32_t)data.quota5hPct, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar5hObj, getQuotaBarColor(data.quota5hPct), LV_PART_INDICATOR);

    // Update Weekly Quota Bar & Labels
    char bufWk[32];
    snprintf(bufWk, sizeof(bufWk), "%.1f%%", data.quotaWeeklyPct);
    lv_label_set_text(labelWkPctObj, bufWk);

    char bufWkReset[48];
    snprintf(bufWkReset, sizeof(bufWkReset), "Reset: %s", data.quotaWeeklyResetTime.c_str());
    lv_label_set_text(labelWkResetObj, bufWkReset);

    lv_bar_set_value(barWkObj, (int32_t)data.quotaWeeklyPct, LV_ANIM_ON);
    lv_obj_set_style_bg_color(barWkObj, getQuotaBarColor(data.quotaWeeklyPct), LV_PART_INDICATOR);

    // Overlay Card Logic
    if (data.reLoginRequired || !data.authValid) {
        lv_obj_clear_flag(overlayObj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(overlayObj, LV_OBJ_FLAG_HIDDEN);
    }
}

void UIManager::updateClaude(const ProviderUIData &data) {
    updateScreenWidgets(data, _pillClaude, _pillLabelClaude,
                        _bar5hClaude, _label5hPctClaude, _label5hResetClaude,
                        _barWkClaude, _labelWkPctClaude, _labelWkResetClaude,
                        _overlayClaude);
}

void UIManager::updateAntigravity(const ProviderUIData &data) {
    updateScreenWidgets(data, _pillAntigravity, _pillLabelAntigravity,
                        _bar5hAntigravity, _label5hPctAntigravity, _label5hResetAntigravity,
                        _barWkAntigravity, _labelWkPctAntigravity, _labelWkResetAntigravity,
                        _overlayAntigravity);
}

void UIManager::showScreen(uint8_t screenIdx) {
    _activeScreen = screenIdx % 2;
    updateIndicatorDots();
}

void UIManager::updateIndicatorDots() {
    if (_activeScreen == 0) {
        lv_obj_set_style_bg_color(_dot1, lv_color_hex(0x38BDF8), LV_PART_MAIN); // Active Sky Blue
        lv_obj_set_style_bg_color(_dot2, lv_color_hex(0x334155), LV_PART_MAIN); // Dimmed Slate
    } else {
        lv_obj_set_style_bg_color(_dot1, lv_color_hex(0x334155), LV_PART_MAIN);
        lv_obj_set_style_bg_color(_dot2, lv_color_hex(0x38BDF8), LV_PART_MAIN);
    }
}

void UIManager::handleSwipeGesture(lv_dir_t dir) {
    if (dir == LV_DIR_LEFT && _activeScreen == 0) {
        lv_tileview_set_tile_act(_screenTileview, 1, 0, LV_ANIM_ON);
        showScreen(1);
    } else if (dir == LV_DIR_RIGHT && _activeScreen == 1) {
        lv_tileview_set_tile_act(_screenTileview, 0, 0, LV_ANIM_ON);
        showScreen(0);
    }
}
