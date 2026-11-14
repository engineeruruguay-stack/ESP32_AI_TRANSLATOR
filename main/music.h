#pragma once
#include <stdbool.h>

bool music_scan(void);
bool music_active(void);
void music_stop(void);
bool music_select(int step);
bool music_tick(void);
