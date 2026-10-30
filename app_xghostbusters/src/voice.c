#include <math.h>
#include <vnr.h>
#include <xcore/hwtimer.h>
#include "app.h"

static vnr_state_t state;
static int32_t input[VNR_FRAME_ADVANCE] __attribute__((aligned(8)));
static unsigned fill_count, attack, hold, active, score;
static unsigned audible;
struct voice_diagnostics {
    unsigned frames, active, score_milli, speech_frames, transitions;
    unsigned process_ticks, max_process_ticks;
    int rms_dbfs;
};
volatile struct voice_diagnostics voice_diag;

void voice_init(void) {vnr_state_init(&state);}
unsigned voice_active(void) {return active;}
unsigned voice_score(void) {return score;}
unsigned voice_audible(void) {return audible;}

void voice_samples(const int32_t samples[],unsigned count) {
    for(unsigned i=0;i<count;++i) {
        input[fill_count++]=samples[i];
        if(fill_count!=VNR_FRAME_ADVANCE)continue;
        fill_count=0;
        unsigned start=get_reference_time();
        double energy=0;
        for(unsigned j=0;j<VNR_FRAME_ADVANCE;++j) {
            double v=input[j];energy+=v*v;
        }
        int db=(int)(10*log10(energy/(VNR_FRAME_ADVANCE*2147483648.0*2147483648.0)+1e-15));
        audible=db>-80;
        float_s32_t prediction;
        vnr_process_frame(&state,&prediction,input);
        float estimate=float_s32_to_f32(prediction);
        score=(unsigned)(fminf(1,fmaxf(0,estimate))*1000);
        unsigned previous=active;
        // VNR is an estimated speech/noise strength, not a probability.
        // Hysteresis, a two-frame attack and 300 ms hangover avoid flicker.
        if(db>-80 && score>=(active?450:600)) {
            if(attack<2)++attack;
            if(attack>=2){active=1;hold=20;}
        } else {
            attack=0;
            if(hold)--hold;
            else active=0;
        }
        ++voice_diag.frames;
        voice_diag.active=active;voice_diag.score_milli=score;
        voice_diag.rms_dbfs=db;
        if(active)++voice_diag.speech_frames;
        if(previous!=active)++voice_diag.transitions;
        voice_diag.process_ticks=get_reference_time()-start;
        if(voice_diag.process_ticks>voice_diag.max_process_ticks)
            voice_diag.max_process_ticks=voice_diag.process_ticks;
    }
}
