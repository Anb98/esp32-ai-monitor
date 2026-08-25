#include "audio_es8311.h"
#include <Wire.h>
#include <driver/i2s.h>
#include <driver/gpio.h>
#include "esp_log.h"
#include <math.h>

static const char *TAG = "ES8311";

AudioES8311 audio;

AudioES8311::AudioES8311() : _volume(80), _initialized(false) {}

void AudioES8311::initI2C() {
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(0x00); // Reset
    Wire.write(0x1F);
    Wire.endTransmission();

    // Clock manager & DAC setup
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(0x01);
    Wire.write(0x30);
    Wire.endTransmission();

    // DAC Power up
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(0x02);
    Wire.write(0x00);
    Wire.endTransmission();

    // DAC Volume setup
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(0x32);
    Wire.write(0xBF);
    Wire.endTransmission();

    // Set power amplifier pin
    gpio_set_direction((gpio_num_t)PA_ENABLE_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)PA_ENABLE_PIN, 1);
}

void AudioES8311::initI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = I2S_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .mck_io_num = I2S_MCLK_PIN,
        .bck_io_num = I2S_BCLK_PIN,
        .ws_io_num = I2S_WS_PIN,
        .data_out_num = I2S_DOUT_PIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    esp_err_t err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    if (err == ESP_OK) {
        i2s_set_pin(I2S_NUM_0, &pin_config);
    } else {
        ESP_LOGE(TAG, "Failed to install I2S driver: %s", esp_err_to_name(err));
    }
}

bool AudioES8311::init() {
    ESP_LOGI(TAG, "Initializing ES8311 audio codec...");
    initI2C();
    initI2S();
    _initialized = true;
    ESP_LOGI(TAG, "ES8311 audio initialized.");
    return true;
}

void AudioES8311::playTone(float freqHz, uint32_t durationMs) {
    if (!_initialized || freqHz <= 0) return;

    size_t totalSamples = (I2S_SAMPLE_RATE * durationMs) / 1000;
    int16_t buffer[256];
    size_t bytesWritten = 0;
    float phase = 0.0f;
    float phaseIncrement = (2.0f * (float)M_PI * freqHz) / (float)I2S_SAMPLE_RATE;
    float amplitude = 12000.0f * (_volume / 100.0f);

    size_t samplesRemaining = totalSamples;
    while (samplesRemaining > 0) {
        size_t chunk = (samplesRemaining > 128) ? 128 : samplesRemaining;
        for (size_t i = 0; i < chunk; i++) {
            int16_t val = (int16_t)(sinf(phase) * amplitude);
            buffer[i * 2] = val;     // Left channel
            buffer[i * 2 + 1] = val; // Right channel
            phase += phaseIncrement;
            if (phase > 2.0f * (float)M_PI) {
                phase -= 2.0f * (float)M_PI;
            }
        }
        i2s_write(I2S_NUM_0, buffer, chunk * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
        samplesRemaining -= chunk;
    }
}

void AudioES8311::playDegradationAlert() {
    ESP_LOGI(TAG, "Playing degradation warning tone (440Hz -> 330Hz)...");
    playTone(440.0f, 200);
    vTaskDelay(pdMS_TO_TICKS(50));
    playTone(330.0f, 300);
}

void AudioES8311::playRecoveryChime() {
    ESP_LOGI(TAG, "Playing operational recovery chime (C5 -> E5 -> G5)...");
    playTone(523.25f, 150); // C5
    vTaskDelay(pdMS_TO_TICKS(30));
    playTone(659.25f, 150); // E5
    vTaskDelay(pdMS_TO_TICKS(30));
    playTone(783.99f, 250); // G5
}

void AudioES8311::setVolume(uint8_t volume) {
    if (volume > 100) volume = 100;
    _volume = volume;
}

void AudioES8311::stop() {
    i2s_zero_dma_buffer(I2S_NUM_0);
}
