#pragma once

#include <stdbool.h>
#include "audio.h"

bool translator_init(void);
const char *translator_recognize_speech(const audio_frame_t *frame);
const char *translator_translate(const char *spanish_text);
