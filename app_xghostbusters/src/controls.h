#pragma once

/* Updated once per 512-sample audio hop (32 ms), independently of LCD timing. */
typedef struct {
    unsigned last_pitch, tonal_hold, speech_frames, onset;
    unsigned hum, fire;
} controls_t;
void controls_step(controls_t *c,unsigned pitch,unsigned vad,
                   unsigned score,unsigned audible);
