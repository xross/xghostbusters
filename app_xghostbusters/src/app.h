#pragma once
#include <stdint.h>
#include <xccompat.h>
#define BARS 32
#define FFT_N 1024
#define HOP 512
#define FRAME 32
#define SPECTRUM_WORDS (BARS+10)
void voice_init(void);
void voice_samples(const int32_t samples[], unsigned count);
unsigned voice_active(void);
unsigned voice_score(void);
unsigned voice_audible(void);
void microphone_init(void);
void audio_capture(unsigned c_pcm);
void spectrum_task(unsigned c_pcm, unsigned c_spectrum);
void display_task(unsigned c_display);
void spectrum_self_test(void);
