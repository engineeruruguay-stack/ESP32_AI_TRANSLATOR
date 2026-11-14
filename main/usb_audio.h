#pragma once
#include <stdbool.h>

bool usb_audio_enter(void);
void usb_audio_leave(void);
bool usb_audio_tick(void);
