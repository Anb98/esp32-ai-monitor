#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "config.h"

enum class SoundType {
    NONE,
    DEGRADATION_ALERT,
    RECOVERY_CHIME
};

class AudioES8311 {
public:
    AudioES8311();
    bool init();
    void createQueue();
    void triggerDegradationAlert();
    void triggerRecoveryChime();
    void taskLoop();
    void setVolume(uint8_t volume); // 0 - 100
    void stop();

private:
    void initI2C();
    void initI2S();
    void playSequence(SoundType type);
    void playTone(float freqHz, uint32_t durationMs);

    uint8_t _volume;
    bool _initialized;
    QueueHandle_t _queue;
};

extern AudioES8311 audio;
