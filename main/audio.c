#include "audio.h"
#include <stdlib.h>
#include <string.h>

static int16_t s_dummy_buffer[1024];

bool audio_init(void)
{
    // TODO: Инициализировать аудиоплату ESP32-S3 и периферию.
    memset(s_dummy_buffer, 0, sizeof(s_dummy_buffer));
    return true;
}

bool audio_capture(audio_frame_t *frame)
{
    if (frame == NULL) {
        return false;
    }

    // TODO: Считать данные с микрофона и заполнить frame.samples.
    frame->samples = s_dummy_buffer;
    frame->length = 0; // Установите реальную длину после захвата.
    return false;
}

void audio_frame_release(audio_frame_t *frame)
{
    if (frame == NULL) {
        return;
    }
    frame->samples = NULL;
    frame->length = 0;
}
