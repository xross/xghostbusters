#include "pitch.h"

unsigned pitch_detect(const int32_t samples[1024]) {
    /* Average/decimate to 4 kHz, remove DC, then a short YIN difference
       function. First periodic minimum favours the fundamental over harmonics. */
    float x[256], difference[52], cmnd[52], mean=0, energy=0;
    for(unsigned i=0;i<256;++i) {
        double sum=0;
        for(unsigned j=0;j<4;++j)sum+=samples[4*i+j];
        x[i]=(float)(sum/(4.0*2147483648.0));mean+=x[i];
    }
    mean/=256;
    for(unsigned i=0;i<256;++i){x[i]-=mean;energy+=x[i]*x[i];}
    /* -78 dBFS RMS gate matches the low digital level of these board mics.
       Periodicity below still rejects non-tonal background noise. */
    if(energy<256*0.000000015849f)return 0;
    float total=0;
    cmnd[0]=1;
    for(unsigned lag=1;lag<=51;++lag) {
        float sum=0;
        for(unsigned i=0;i<192;++i) {
            float delta=x[i]-x[i+lag];sum+=delta*delta;
        }
        difference[lag]=sum;total+=sum;
        cmnd[lag]=total>0?sum*lag/total:1;
    }
    for(unsigned lag=8;lag<=50;++lag) {
        if(cmnd[lag]>=0.18f)continue;
        while(lag<50 && cmnd[lag+1]<cmnd[lag])++lag;
        float refined=(float)lag;
        float a=difference[lag-1],b=difference[lag],c=difference[lag+1];
        float denominator=2*(a-2*b+c);
        if(denominator>0)refined+=(a-c)/denominator;
        unsigned hz=(unsigned)(4000/refined+0.5f);
        return hz>=80 && hz<=500?hz:0;
    }
    return 0;
}
