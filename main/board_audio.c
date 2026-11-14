#include "board_audio.h"

#include <stdlib.h>
#include "driver/i2s_std.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"

static const char *TAG = "board_audio";
enum { OUTPUT_PERCENT = 10 };
static i2s_chan_handle_t tx, rx;
static i2c_master_dev_handle_t tca;
static bool mic_ok, speaker_ok;
static const audio_codec_if_t *adc, *dac;

bool board_amp(bool enabled)
{
    if (!tca) {
        return false;
    }
    const uint8_t reg = 0x03;
    uint8_t output;
    if (i2c_master_transmit_receive(tca, &reg, 1, &output, 1, 50) != ESP_OK) {
        return false;
    }
    uint8_t write[] = {reg, (output & 0xfe) | enabled};
    return i2c_master_transmit(tca, write, sizeof(write), 50) == ESP_OK;
}

void board_audio_init(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t expander)
{
    tca = expander;
    if (board_amp(false)) {
        const uint8_t reg = 0x07;
        uint8_t direction;
        if (i2c_master_transmit_receive(tca, &reg, 1, &direction, 1, 50) == ESP_OK) {
            uint8_t cfg[] = {reg, (direction | 0x0e) & 0xfe};
            if (i2c_master_transmit(tca, cfg, sizeof(cfg), 50) != ESP_OK) {
                tca = NULL;
            }
        } else {
            tca = NULL;
        }
    } else {
        tca = NULL;
    }

    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    channel.auto_clear = true;
    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                       I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = 12, .bclk = 13, .ws = 14, .din = 15, .dout = 16},
    };
    if (i2s_new_channel(&channel, &tx, &rx) != ESP_OK ||
        i2s_channel_init_std_mode(tx, &cfg) != ESP_OK ||
        i2s_channel_init_std_mode(rx, &cfg) != ESP_OK ||
        i2s_channel_enable(tx) != ESP_OK || i2s_channel_enable(rx) != ESP_OK) {
        ESP_LOGE(TAG, "I2S initialization failed");
        return;
    }

    esp_codec_dev_sample_info_t format = {
        .bits_per_sample = 16, .channel = 2, .sample_rate = AUDIO_RATE,
    };
    audio_codec_i2c_cfg_t ctrl = {.port = 0, .bus_handle = bus};
    esp_err_t probe = i2c_master_probe(bus, 0x40, 50);
    ESP_LOGI(TAG, "ES7210 0x40: %s", esp_err_to_name(probe));
    if (probe == ESP_OK) {
        ctrl.addr = ES7210_CODEC_DEFAULT_ADDR;
        es7210_codec_cfg_t mic = {
            .ctrl_if = audio_codec_new_i2c_ctrl(&ctrl),
            .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2,
        };
        adc = es7210_codec_new(&mic);
        mic_ok = adc && adc->set_fs(adc, &format) == 0 &&
                 adc->set_mic_gain(adc, 24) == 0 && adc->enable(adc, true) == 0;
    }
    probe = i2c_master_probe(bus, 0x18, 50);
    ESP_LOGI(TAG, "ES8311 0x18: %s", esp_err_to_name(probe));
    if (probe == ESP_OK) {
        ctrl.addr = ES8311_CODEC_DEFAULT_ADDR;
        es8311_codec_cfg_t speaker = {
            .ctrl_if = audio_codec_new_i2c_ctrl(&ctrl),
            .gpio_if = audio_codec_new_gpio(),
            .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
            .pa_pin = -1,
            .use_mclk = true,
        };
        dac = es8311_codec_new(&speaker);
        speaker_ok = dac && tca && dac->set_fs(dac, &format) == 0 &&
                     dac->enable(dac, true) == 0 && dac->set_vol(dac, 0) == 0;
    }
    ESP_LOGI(TAG, "Capture %s, speaker %s", mic_ok ? "ready" : "unavailable",
             speaker_ok ? "ready" : "unavailable");
}

bool board_mic_ready(void) { return mic_ok; }
bool board_speaker_ready(void) { return speaker_ok; }

bool board_read(int16_t *mono, size_t frames, bool report_peaks)
{
    int16_t stereo[AUDIO_BLOCK * 2];
    size_t bytes;
    if (!mic_ok || frames > AUDIO_BLOCK ||
        i2s_channel_read(rx, stereo, frames * 4, &bytes, 100) != ESP_OK ||
        bytes != frames * 4) {
        return false;
    }
    int left_peak = 0, right_peak = 0;
    for (size_t i = 0; i < frames; ++i) {
        int left = stereo[2 * i], right = stereo[2 * i + 1];
        mono[i] = (left + right) / 2;
        if (abs(left) > left_peak) {
            left_peak = abs(left);
        }
        if (abs(right) > right_peak) {
            right_peak = abs(right);
        }
    }
    if (report_peaks) {
        ESP_LOGI(TAG, "ADC slot peaks: L=%d R=%d / 32768", left_peak, right_peak);
    }
    return true;
}

bool board_write(const int16_t *mono, size_t frames)
{
    int16_t stereo[AUDIO_BLOCK * 2];
    size_t bytes;
    if (!speaker_ok || frames > AUDIO_BLOCK) {
        return false;
    }
    for (size_t i = 0; i < frames; ++i) {
        // Apply the same fixed ceiling to music, diagnostics and gateway audio.
        int16_t sample = (int32_t)mono[i] * OUTPUT_PERCENT / 100;
        stereo[2 * i] = sample;
        stereo[2 * i + 1] = sample;
    }
    return i2s_channel_write(tx, stereo, frames * 4, &bytes, 150) == ESP_OK &&
           bytes == frames * 4;
}

void board_diagnostics(void)
{
    int16_t block[AUDIO_BLOCK];
    if (board_speaker_ready() && board_amp(true)) {
        for (size_t i = 0; i < AUDIO_BLOCK; ++i) {
            block[i] = i % 32 < 16 ? 2200 : -2200;
        }
        bool ok = true;
        for (int i = 0; i < 10 && ok; ++i) {
            ok = board_write(block, AUDIO_BLOCK);
        }
        for (size_t i = 0; i < AUDIO_BLOCK; ++i) {
            block[i] = 0;
        }
        for (int i = 0; i < 8 && ok; ++i) {
            ok = board_write(block, AUDIO_BLOCK);
        }
        bool muted = board_amp(false);
        ESP_LOGI(TAG, "Speaker tone write: %s", ok && muted ? "OK" : "failed");
    }
    if (board_mic_ready()) {
        ESP_LOGI(TAG, "Speak or tap near each microphone to check both slots");
        for (int i = 0; i < 100; ++i) {
            if (!board_read(block, AUDIO_BLOCK, i % 10 == 0)) {
                ESP_LOGE(TAG, "Microphone read failed");
                break;
            }
        }
    }
}
