#pragma once
#include <stdbool.h>

bool stream_start(bool spanish_source);
void stream_finish(void);
void stream_stop(void);
bool stream_active(void);
bool stream_tick(void);
