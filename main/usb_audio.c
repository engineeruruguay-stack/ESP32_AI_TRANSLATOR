#include "usb_audio.h"
#include "board_audio.h"

#include <stdatomic.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "usb_device_uac.h"

_Static_assert(CONFIG_UAC_SPEAKER_CHANNEL_NUM == 2 &&
               CONFIG_UAC_SAMPLE_RATE == 48000 && CONFIG_UAC_BYTES_PER_SAMPLE == 2 &&
               AUDIO_RATE == 16000,
               "USB downmix requires stereo 48 kHz input and mono 16 kHz output");

static StreamBufferHandle_t audio;
static atomic_bool active, muted;
static bool initialized, amp_on;

static void set_mute(uint32_t mute, void *context)
{
    muted = mute != 0;
}

static void queue_pcm(const int16_t *pcm, size_t samples)
{
    size_t bytes = samples * sizeof(*pcm);
    if (active && !muted && xStreamBufferSpacesAvailable(audio) >= bytes) {
        xStreamBufferSend(audio, pcm, bytes, 0);
    }
}

static esp_err_t receive_audio(uint8_t *data, size_t len, void *context)
{
    static int sum, count;
    int16_t pcm[AUDIO_BLOCK];
    size_t used = 0;
    for (size_t i = 0; i + 4 <= len; i += 4) {
        int16_t stereo[2];
        memcpy(stereo, data + i, sizeof(stereo));
        sum += stereo[0] + stereo[1];
        if (++count == 3) {
            pcm[used++] = sum / 6;
            sum = count = 0;
            if (used == AUDIO_BLOCK) {
                queue_pcm(pcm, used);
                used = 0;
            }
        }
    }
    if (used) {
        queue_pcm(pcm, used);
    }
    return ESP_OK;
}

bool usb_audio_enter(void)
{
    if (!board_speaker_ready()) {
        return false;
    }
    if (!initialized) {
        if (!audio) {
            audio = xStreamBufferCreate(8 * AUDIO_BLOCK * sizeof(int16_t), 1);
        }
        uac_device_config_t config = {.output_cb = receive_audio, .set_mute_cb = set_mute};
        if (!audio || uac_device_init(&config) != ESP_OK) {
            return false;
        }
        initialized = true;
    }
    active = true;
    return true;
}

void usb_audio_leave(void)
{
    active = false;
    if (amp_on) {
        board_amp(false);
        amp_on = false;
    }
}

bool usb_audio_tick(void)
{
    if (!audio) {
        return true;
    }
    int16_t pcm[AUDIO_BLOCK];
    size_t bytes = xStreamBufferReceive(audio, pcm, sizeof(pcm), 0);
    if (!active || muted || !bytes) {
        return true;
    }
    if (!amp_on) {
        amp_on = board_amp(true);
    }
    return amp_on && board_write(pcm, bytes / sizeof(*pcm));
}
