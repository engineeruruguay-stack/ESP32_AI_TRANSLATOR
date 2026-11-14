#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/i2c_master.h"

#define AUDIO_RATE 16000
#define AUDIO_BLOCK 320

void board_audio_init(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t expander);
bool board_mic_ready(void);
bool board_speaker_ready(void);
bool board_read(int16_t *mono, size_t frames, bool report_peaks);
bool board_write(const int16_t *mono, size_t frames);
bool board_amp(bool enabled);
void board_diagnostics(void);
void board_adjust_volume(int step);
