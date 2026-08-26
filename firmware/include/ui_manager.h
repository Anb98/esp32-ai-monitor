#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"

struct ProviderUIData {
    String id;
    String name;
    String status; // "operational", "degraded", "outage"
    bool authValid;
    bool reLoginRequired;
    String authState; // "valid", "expired", "unknown"
    bool stale;
    float quota5hUsed;
    float quota5hLimit;
    float quota5hPct;
    String quota5hResetTime;
    bool quota5hAvailable;
    int32_t quota5hResetIn;
    float quotaWeeklyUsed;
    float quotaWeeklyLimit;
    float quotaWeeklyPct;
    String quotaWeeklyResetTime;
    bool quotaWeeklyAvailable;
    int32_t quotaWeeklyResetIn;
};

class UIManager {
public:
    UIManager();
    void init();
    void createLock();
    bool uiLock();
    void uiUnlock();
    void updateClaude(const ProviderUIData &data);

private:
    // Countdown seed state, resynced from each poll and ticked locally by a
    // 1Hz LVGL timer (the firmware has no NTP/wall clock). Declared ahead of
    // the methods because tickOneCountdown takes it by reference.
    struct CountdownState {
        bool available;
        int32_t seedSeconds;
        uint32_t seedAtMs;
    };

    void createScreenViews();
    void buildProviderScreen(const char *title, lv_obj_t **screenObj,
                             lv_obj_t **pillObj, lv_obj_t **pillLabelObj, lv_obj_t **staleBadgeObj,
                             lv_obj_t **bar5hObj, lv_obj_t **label5hPctObj, lv_obj_t **label5hResetObj,
                             lv_obj_t **barWkObj, lv_obj_t **labelWkPctObj, lv_obj_t **labelWkResetObj,
                             lv_obj_t **overlayObj,
                             lv_obj_t **countdownCaptionObj, lv_obj_t **countdownValueObj,
                             lv_obj_t **countdownCaptionWeeklyObj, lv_obj_t **countdownValueWeeklyObj);
    void updateScreenWidgets(const ProviderUIData &data,
                             lv_obj_t *pillObj, lv_obj_t *pillLabelObj, lv_obj_t *staleBadgeObj,
                             lv_obj_t *bar5hObj, lv_obj_t *label5hPctObj, lv_obj_t *label5hResetObj,
                             lv_obj_t *barWkObj, lv_obj_t *labelWkPctObj, lv_obj_t *labelWkResetObj,
                             lv_obj_t *overlayObj);
    void tickCountdown();
    void tickOneCountdown(CountdownState &state, lv_obj_t *captionObj, lv_obj_t *valueObj,
                          const char *captionText);
    lv_color_t getQuotaBarColor(float pct);
    static void countdownTimerCb(lv_timer_t *timer);

    SemaphoreHandle_t _lock;
    lv_obj_t *_cardClaude;

    CountdownState _countdownClaude;
    CountdownState _countdownWeeklyClaude;

    // Claude widgets
    lv_obj_t *_pillClaude;
    lv_obj_t *_pillLabelClaude;
    lv_obj_t *_staleBadgeClaude;
    lv_obj_t *_bar5hClaude;
    lv_obj_t *_label5hPctClaude;
    lv_obj_t *_label5hResetClaude;
    lv_obj_t *_barWkClaude;
    lv_obj_t *_labelWkPctClaude;
    lv_obj_t *_labelWkResetClaude;
    lv_obj_t *_overlayClaude;
    lv_obj_t *_countdownCaptionClaude;
    lv_obj_t *_countdownValueClaude;
    lv_obj_t *_countdownCaptionWeeklyClaude;
    lv_obj_t *_countdownValueWeeklyClaude;
};

extern UIManager ui;
