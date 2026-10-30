#include <math.h>
#include <string.h>
#include <stdint.h>
#include <xmath/xmath.h>
#include <mic_array.h>
#include <xcore/channel.h>
#include <xcore/hwtimer.h>
#include <xcore/assert.h>
#include "app.h"
#include "pitch.h"
#include "controls.h"

struct audio_diagnostics {
    unsigned frames, spectra, samples, sample_ticks;
    unsigned fft_ticks, max_fft_ticks, selected_mic;
    int peak[2], rms_db[2], dominant_hz, self_test_pass;
    unsigned hum_hz;
};
volatile struct audio_diagnostics audio_diag;
volatile controls_t controls_diag;
#define QUEUE_FRAMES 64
static int32_t audio_queue[QUEUE_FRAMES][2][FRAME];
static volatile unsigned queue_write,queue_read;
volatile unsigned capture_overruns,capture_frames,capture_second_ticks;

void audio_capture(unsigned c_pcm) {
    unsigned tick=get_reference_time();
    while(1) {
        unsigned w=queue_write;
        int32_t discard[2][FRAME];
        int full=(unsigned)(w-queue_read)>=QUEUE_FRAMES;
        int32_t *target=full?&discard[0][0]:&audio_queue[w%QUEUE_FRAMES][0][0];
        ma_frame_rx(target,c_pcm,2,FRAME);
        if(full)++capture_overruns;
        else {asm volatile("" ::: "memory");queue_write=w+1;}
        if((++capture_frames%500)==0) {
            unsigned now=get_reference_time();capture_second_ticks=now-tick;tick=now;
        }
    }
}
static int32_t history[FFT_N] __attribute__((aligned(8)));
static int32_t work[FFT_N+2] __attribute__((aligned(8)));
static int32_t window[FFT_N] __attribute__((aligned(8)));
static unsigned edges[BARS+1];
static float smoothed[BARS];

static void init_dsp(void) {
    for (unsigned i=0; i<FFT_N; ++i)
        window[i] = (int32_t)((0.5 - 0.5*cos(2.0*3.141592653589793*i/FFT_N))*2147483647.0);
    for (unsigned i=0; i<=BARS; ++i) {
        unsigned bin = (unsigned)round(4.0*pow(128.0, (double)i/BARS));
        edges[i] = bin > FFT_N/2 ? FFT_N/2 : bin;
    }
    for (unsigned i=0; i<BARS; ++i) smoothed[i]=-90;
}

static void transform(float db[BARS], unsigned *dominant) {
    for (unsigned i=0; i<FFT_N; ++i)
        work[i] = (int32_t)(((int64_t)history[i]*window[i]) >> 31);
    bfp_s32_t input;
    bfp_s32_init(&input, work, -31, FFT_N, 1);
    bfp_complex_s32_t *fft = bfp_fft_forward_mono(&input);
    bfp_fft_unpack_mono(fft);
    double max_power=0;
    unsigned best=0;
    // Include BFP exponent and Hann coherent gain; 0 dBFS = full-scale sine.
    double scale = ldexp(1.0, 2*fft->exp) * (16.0/(FFT_N*FFT_N));
    for (unsigned b=0; b<BARS; ++b) {
        double band=0;
        unsigned end=edges[b+1];
        if (end<=edges[b]) end=edges[b]+1;
        for (unsigned k=edges[b]; k<end && k<FFT_N/2; ++k) {
            double re=fft->data[k].re, im=fft->data[k].im;
            double p=re*re+im*im;
            if (p>band) band=p;
            if (p>max_power) {max_power=p; best=k;}
        }
        db[b]=(float)(10.0*log10(band*scale+1e-15));
    }
    *dominant=(best*16000+FFT_N/2)/FFT_N;
}

void spectrum_self_test(void) {
    float db[BARS]; unsigned dominant;
    for (unsigned i=0; i<FFT_N; ++i)
        history[i]=(int32_t)(1073741824.0*sin(2.0*3.141592653589793*64*i/FFT_N));
    transform(db,&dominant);
    float peak=-150;
    for (unsigned i=0; i<BARS; ++i) if(db[i]>peak) peak=db[i];
    xassert(dominant==1000 && peak>-6.2f && peak<-5.8f);
    memset(history,0,sizeof(history));
    transform(db,&dominant);
    for(unsigned i=0;i<BARS;++i) xassert(db[i]<-100);
    audio_diag.self_test_pass=1;
}

void spectrum_task(unsigned c_pcm, unsigned c_spectrum) {
    (void)c_pcm;
    init_dsp();
    spectrum_self_test();
    voice_init();
    int32_t frame[2][FRAME];
    unsigned filled=0, selected=0, ticks=get_reference_time();
    double energy[2]={0,0};
    uint32_t packet[SPECTRUM_WORDS]={0};
    controls_t controls={0};
    while(1) {
        while(queue_read==queue_write) asm volatile("" ::: "memory");
        memcpy(frame,audio_queue[queue_read%QUEUE_FRAMES],sizeof(frame));
        asm volatile("" ::: "memory");
        ++queue_read;
        unsigned peaks[2]={0,0};
        for(unsigned m=0;m<2;++m) for(unsigned i=0;i<FRAME;++i) {
            double v=frame[m][i]; energy[m]+=v*v;
            unsigned a=frame[m][i]<0 ? -(int64_t)frame[m][i] : frame[m][i];
            if(a>peaks[m]) peaks[m]=a;
        }
        for(unsigned m=0;m<2;++m) if(peaks[m]>(unsigned)audio_diag.peak[m]) audio_diag.peak[m]=peaks[m];
        // Choose a valid clock edge after the initial settling interval. A
        // disconnected edge tends to produce rails; reject it using RMS level.
        if(audio_diag.frames==249) {
            double limit=0.25*2147483648.0*2147483648.0*8000;
            if(energy[0]>limit && energy[1]<limit) selected=1;
            else if(energy[1]>limit && energy[0]<limit) selected=0;
            else selected=energy[1]>energy[0] ? 1:0;
        }
        memcpy(history+FFT_N-HOP+filled,frame[selected],FRAME*sizeof(int32_t));
        // Allow microphone and DC filter startup to settle before detection.
        if(audio_diag.frames>=500) voice_samples(frame[selected],FRAME);
        filled+=FRAME;
        ++audio_diag.frames; audio_diag.samples+=FRAME;
        if((audio_diag.frames%500)==0) {
            unsigned now=get_reference_time();
            audio_diag.sample_ticks=now-ticks; ticks=now;
            for(unsigned m=0;m<2;++m)
                audio_diag.rms_db[m]=(int)(10*log10(energy[m]/(16000.0*2147483648.0*2147483648.0)+1e-15));
            energy[0]=energy[1]=0;
        }
        if(filled==HOP) {
            float db[BARS]; unsigned dominant;
            unsigned start=get_reference_time();
            transform(db,&dominant);
            for(unsigned b=0;b<BARS;++b) {
                float alpha=db[b]>smoothed[b]?0.8f:0.18f;
                smoothed[b]+=alpha*(db[b]-smoothed[b]);
                float level=(smoothed[b]+85.0f)/65.0f;
                packet[b]=(unsigned)(fminf(1,fmaxf(0,level))*220);
            }
            packet[BARS]=++audio_diag.spectra;
            packet[BARS+1]=dominant;
            packet[BARS+2]=selected;
            packet[BARS+3]=audio_diag.samples;
            packet[BARS+4]=voice_active();
            packet[BARS+5]=voice_score();
            unsigned hum=audio_diag.frames>=500?pitch_detect(history):0;
            packet[BARS+6]=hum;
            packet[BARS+7]=hum!=0;
            controls_step(&controls,hum,voice_active(),voice_score(),voice_audible());
            packet[BARS+8]=controls.fire;
            packet[BARS+9]=controls.hum;
            controls_diag=controls;
            audio_diag.hum_hz=hum;
            audio_diag.dominant_hz=dominant; audio_diag.selected_mic=selected;
            audio_diag.fft_ticks=get_reference_time()-start;
            if(audio_diag.fft_ticks>audio_diag.max_fft_ticks) audio_diag.max_fft_ticks=audio_diag.fft_ticks;
            for(unsigned i=0;i<SPECTRUM_WORDS;++i) chan_out_word(c_spectrum,packet[i]);
            memmove(history,history+HOP,HOP*sizeof(int32_t));
            filled=0;
        }
    }
}
