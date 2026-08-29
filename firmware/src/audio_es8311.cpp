#include "audio_es8311.h"
#include "notification_pcm.h"
#include <Wire.h>
#include <driver/i2s.h>
#include <driver/gpio.h>
#include "esp_log.h"
#include <math.h>

static const char *TAG = "ES8311";

AudioES8311 audio;

AudioES8311::AudioES8311() : _volume(70), _initialized(false), _queue(nullptr) {}

void AudioES8311::createQueue() {
    _queue = xQueueCreate(2, sizeof(SoundType));
}

bool AudioES8311::writeReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool AudioES8311::writeRegRetry(uint8_t reg, uint8_t value) {
    if (writeReg(reg, value)) return true;
    ESP_LOGW(TAG, "ES8311 did not ACK reg 0x%02X, retrying", reg);
    return writeReg(reg, value); // the chip's first I2C write after power-on can fail
}

// Bring-up sequence flattened from Espressif's esp_codec_dev es8311 driver
// (es8311_open + es8311_config_sample + es8311_start) for our fixed config:
// I2S slave, MCLK from GPIO at 256*fs, 16 kHz, 16-bit, DAC playback path.
bool AudioES8311::initI2C() {
    bool ok = writeRegRetry(0x00, 0x1F); // full digital reset — transaction #1
    vTaskDelay(pdMS_TO_TICKS(20));
    ok &= writeRegRetry(0x00, 0x00); // release reset — a lost release leaves the whole register file undefined

    // Deliberately `&=`, not `&&=`: every register below must still be written
    // even after an earlier NAK, so bring-up never stops partway through.
    ok &= writeReg(0x44, 0x08);
    ok &= writeReg(0x01, 0x30);
    ok &= writeReg(0x02, 0x00); // pre_div=1, pre_mult=1 (MCLK = 256*fs)
    ok &= writeReg(0x03, 0x10); // ADC osr
    ok &= writeReg(0x16, 0x24);
    ok &= writeReg(0x04, 0x20); // DAC osr
    ok &= writeReg(0x05, 0x00); // adc_div=1, dac_div=1
    ok &= writeReg(0x06, 0x03); // bclk_div=4, SCLK not inverted
    ok &= writeReg(0x07, 0x00); // lrck_h
    ok &= writeReg(0x08, 0xFF); // lrck_l
    ok &= writeReg(0x0B, 0x00);
    ok &= writeReg(0x0C, 0x00);
    ok &= writeReg(0x10, 0x1F);
    ok &= writeReg(0x11, 0x7F);
    ok &= writeReg(0x00, 0x80); // CSM power on, slave mode; the chip stays silent without this
    ok &= writeReg(0x01, 0x3F); // internal MCLK from MCLK pin, all clocks on
    ok &= writeReg(0x13, 0x10); // enable output to HP drive
    ok &= writeReg(0x1B, 0x0A);
    ok &= writeReg(0x1C, 0x6A);
    ok &= writeReg(0x09, 0x0C); // SDP in: I2S, 16-bit
    ok &= writeReg(0x0A, 0x0C); // SDP out: I2S, 16-bit
    ok &= writeReg(0x0E, 0x02); // power up DAC analog
    ok &= writeReg(0x12, 0x00); // power up DAC
    ok &= writeReg(0x14, 0x1A);
    ok &= writeReg(0x0D, 0x01); // power up analog bias
    ok &= writeReg(0x15, 0x40);
    ok &= writeReg(0x37, 0x08); // DAC ramp rate
    ok &= writeReg(0x45, 0x00);
    ok &= writeReg(0x32, 0xBF); // DAC volume ~0 dB

    // PA starts low: the amp is powered only while playSequence() is running,
    // which removes the boot pop and the idle draw on battery.
    gpio_set_direction((gpio_num_t)PA_ENABLE_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)PA_ENABLE_PIN, 0);
    return ok;
}

bool AudioES8311::initI2S() {
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
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install I2S driver: %s", esp_err_to_name(err));
        return false;
    }
    err = i2s_set_pin(I2S_NUM_0, &pin_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set I2S pins: %s", esp_err_to_name(err));
    }
    return err == ESP_OK;
}

bool AudioES8311::init() {
    ESP_LOGI(TAG, "Initializing ES8311 audio codec...");
    // I2S first, on purpose. The CSM power-on (0x00=0x80) and the clock manager
    // (0x01=0x3F) latch their dividers against a live MCLK, which only exists
    // once i2s_driver_install/i2s_set_pin drive MCLK/BCLK/WS. The register file
    // itself is I2C-clocked and needs no MCLK, so configuring after the bus is
    // running is safe. Do not reorder back to I2C-first.
    bool ok = initI2S();
    ok &= initI2C();
    _initialized = ok;
    if (!ok) {
        ESP_LOGE(TAG, "ES8311 bring-up failed; notifications disabled");
    } else {
        ESP_LOGI(TAG, "ES8311 audio initialized.");
    }
    return ok;
}

// Chunked buffer feed shared by playPcm and playTone: duplicates each mono
// sample to L/R, applies gain, and advances by the bytes I2S actually wrote
// rather than the requested chunk so a partial write never drops samples.
void AudioES8311::writeMono(const int16_t *samples, size_t count, float gain) {
    int16_t buffer[256];
    size_t pos = 0;
    while (pos < count) {
        size_t chunk = (count - pos > 128) ? 128 : count - pos;
        for (size_t i = 0; i < chunk; i++) {
            int16_t val = (int16_t)(samples[pos + i] * gain);
            buffer[i * 2] = val;     // Left channel
            buffer[i * 2 + 1] = val; // Right channel
        }
        size_t bytesWritten = 0;
        esp_err_t err = i2s_write(I2S_NUM_0, buffer, chunk * 2 * sizeof(int16_t),
                                  &bytesWritten, pdMS_TO_TICKS(200));
        size_t framesWritten = bytesWritten / (2 * sizeof(int16_t));
        // A short write means the DMA queue did not drain within the timeout;
        // advancing by `chunk` would drop those samples. Zero frames means the
        // bus is stalled — bail instead of spinning forever on the same chunk.
        if (err != ESP_OK || framesWritten == 0) {
            ESP_LOGW(TAG, "i2s_write stalled after %u bytes", (unsigned)bytesWritten);
            return;
        }
        pos += framesWritten;
    }
}

void AudioES8311::playTone(float freqHz, uint32_t durationMs) {
    if (!_initialized || freqHz <= 0) return;

    size_t totalSamples = (I2S_SAMPLE_RATE * durationMs) / 1000;
    int16_t mono[128];
    float phase = 0.0f;
    float phaseIncrement = (2.0f * (float)M_PI * freqHz) / (float)I2S_SAMPLE_RATE;
    float amplitude = 12000.0f * (_volume / 100.0f);

    size_t samplesRemaining = totalSamples;
    while (samplesRemaining > 0) {
        size_t chunk = (samplesRemaining > 128) ? 128 : samplesRemaining;
        for (size_t i = 0; i < chunk; i++) {
            mono[i] = (int16_t)(sinf(phase) * amplitude);
            phase += phaseIncrement;
            if (phase > 2.0f * (float)M_PI) {
                phase -= 2.0f * (float)M_PI;
            }
        }
        // gain=1.0f: volume is already baked into amplitude above, and
        // int16_t * 1.0f truncates back to itself, so output stays bit-identical.
        writeMono(mono, chunk, 1.0f);
        samplesRemaining -= chunk;
    }
}

// Streams mono PCM to the stereo I2S bus via the shared writeMono helper.
void AudioES8311::playPcm(const int16_t *samples, size_t count) {
    if (!_initialized) return;
    writeMono(samples, count, _volume / 100.0f);
}

void AudioES8311::playSequence(SoundType type) {
    if (!_initialized) return;
    gpio_set_level((gpio_num_t)PA_ENABLE_PIN, 1);
    switch (type) {
        case SoundType::DEGRADATION_ALERT:
            ESP_LOGI(TAG, "Playing degradation notification sound (x2)...");
            playPcm(NOTIFICATION_PCM, NOTIFICATION_PCM_LEN);
            vTaskDelay(pdMS_TO_TICKS(250));
            playPcm(NOTIFICATION_PCM, NOTIFICATION_PCM_LEN);
            break;
        case SoundType::RECOVERY_CHIME:
            ESP_LOGI(TAG, "Playing operational recovery chime (C5 -> E5 -> G5)...");
            playTone(523.25f, 150); // C5
            vTaskDelay(pdMS_TO_TICKS(30));
            playTone(659.25f, 150); // E5
            vTaskDelay(pdMS_TO_TICKS(30));
            playTone(783.99f, 250); // G5
            break;
        default:
            break;
    }
    stop();
}

void AudioES8311::triggerDegradationAlert() {
    if (!_queue) return;
    SoundType type = SoundType::DEGRADATION_ALERT;
    xQueueSend(_queue, &type, 0);
}

void AudioES8311::triggerRecoveryChime() {
    if (!_queue) return;
    SoundType type = SoundType::RECOVERY_CHIME;
    xQueueSend(_queue, &type, 0);
}

void AudioES8311::taskLoop() {
    if (!_queue) return;
    SoundType type;
    if (xQueueReceive(_queue, &type, portMAX_DELAY) == pdTRUE) {
        playSequence(type);
    }
}

void AudioES8311::setVolume(uint8_t volume) {
    if (volume > 100) volume = 100;
    _volume = volume;
}

void AudioES8311::stop() {
    // PA down before the buffer changes, so the residual-sample discontinuity is
    // never amplified. No settle delay: the amp's own ramp covers it.
    gpio_set_level((gpio_num_t)PA_ENABLE_PIN, 0);
    i2s_zero_dma_buffer(I2S_NUM_0);
}
