#include "controls.h"

void controls_step(controls_t *c,unsigned pitch,unsigned vad,
                   unsigned score,unsigned audible) {
    /* A periodic pitch, including gentle slides/vibrato, is movement input.
       Large discontinuities can be speech; two missing pitch frames are
       tolerated as hum dropouts. VAD alone is never sufficient to fire. */
    unsigned delta=pitch>c->last_pitch?pitch-c->last_pitch:c->last_pitch-pitch;
    unsigned tonal=pitch && (!c->last_pitch || delta*100<=c->last_pitch*12);
    c->last_pitch=pitch;
    if(tonal)c->tonal_hold=3;
    else if(c->tonal_hold)--c->tonal_hold;
    c->hum=audible && (tonal || c->tonal_hold);
    if(!vad || !audible) {
        c->onset=0;c->speech_frames=0;c->fire=0;return;
    }
    if(c->onset<4)++c->onset;
    /* Reject VAD's 300 ms hangover and delay onset until enough non-hum
       evidence has arrived. Four hops also mask noisy hum attacks. */
    if(c->hum || score<450)c->speech_frames=0;
    else if(c->speech_frames<3)++c->speech_frames;
    c->fire=c->onset>=4 && c->speech_frames>=3;
}
