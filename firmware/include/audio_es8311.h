#pragma once

#include <Arduino.h>
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
    void playDegradationAlert();
    void playRecoveryChime();
    void setVolume(uint8_t volume); // 0 - 100
    void stop();

private:
    void initI2C();
    void initI2S();
    void playTone(float freqHz, uint32_t durationMs);

    uint8_t _volume;
    bool _initialized;
};

extern AudioES8311 audio;
