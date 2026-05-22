#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    int16_t *samples;
    size_t length;
} audio_frame_t;

bool audio_init(void);
bool audio_capture(audio_frame_t *frame);
void audio_frame_release(audio_frame_t *frame);
