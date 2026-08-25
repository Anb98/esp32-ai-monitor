#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "config.h"

struct ProviderUIData {
    String id;
    String name;
    String status; // "operational", "degraded", "outage"
    bool authValid;
    bool reLoginRequired;
    float quota5hUsed;
    float quota5hLimit;
    float quota5hPct;
    String quota5hResetTime;
    float quotaWeeklyUsed;
    float quotaWeeklyLimit;
    float quotaWeeklyPct;
    String quotaWeeklyResetTime;
};

class UIManager {
public:
    UIManager();
    void init();
    void updateClaude(const ProviderUIData &data);
    void updateAntigravity(const ProviderUIData &data);
    void showScreen(uint8_t screenIdx);
    void handleSwipeGesture(lv_dir_t dir);

private:
    void createScreenViews();
    void buildProviderScreen(uint8_t screenIdx, const char *title, lv_obj_t **screenObj,
                             lv_obj_t **pillObj, lv_obj_t **pillLabelObj,
                             lv_obj_t **bar5hObj, lv_obj_t **label5hPctObj, lv_obj_t **label5hResetObj,
                             lv_obj_t **barWkObj, lv_obj_t **labelWkPctObj, lv_obj_t **labelWkResetObj,
                             lv_obj_t **overlayObj);
    void updateScreenWidgets(const ProviderUIData &data,
                             lv_obj_t *pillObj, lv_obj_t *pillLabelObj,
                             lv_obj_t *bar5hObj, lv_obj_t *label5hPctObj, lv_obj_t *label5hResetObj,
                             lv_obj_t *barWkObj, lv_obj_t *labelWkPctObj, lv_obj_t *labelWkResetObj,
                             lv_obj_t *overlayObj);
    void updateIndicatorDots();
    lv_color_t getQuotaBarColor(float pct);

    uint8_t _activeScreen;
    lv_obj_t *_screenTileview;
    lv_obj_t *_tileClaude;
    lv_obj_t *_tileAntigravity;

    // Claude widgets
    lv_obj_t *_pillClaude;
    lv_obj_t *_pillLabelClaude;
    lv_obj_t *_bar5hClaude;
    lv_obj_t *_label5hPctClaude;
    lv_obj_t *_label5hResetClaude;
    lv_obj_t *_barWkClaude;
    lv_obj_t *_labelWkPctClaude;
    lv_obj_t *_labelWkResetClaude;
    lv_obj_t *_overlayClaude;

    // Antigravity widgets
    lv_obj_t *_pillAntigravity;
    lv_obj_t *_pillLabelAntigravity;
    lv_obj_t *_bar5hAntigravity;
    lv_obj_t *_label5hPctAntigravity;
    lv_obj_t *_label5hResetAntigravity;
    lv_obj_t *_barWkAntigravity;
    lv_obj_t *_labelWkPctAntigravity;
    lv_obj_t *_labelWkResetAntigravity;
    lv_obj_t *_overlayAntigravity;

    // Page indicator dots
    lv_obj_t *_dot1;
    lv_obj_t *_dot2;
};

extern UIManager ui;
