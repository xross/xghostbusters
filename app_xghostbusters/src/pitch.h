#pragma once
#include <stdint.h>

/* 1024 PCM samples at 16 kHz. Zero means no reliable 80-500 Hz hum. */
unsigned pitch_detect(const int32_t samples[1024]);
