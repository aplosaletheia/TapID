#include <stdlib.h>
#include <stdio.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include "api.h"


//1/audioInfo.sampleDuration

freqDomainST_s dft(audioInfo_s audioInfo, float phase)
{
    freqDomainST_s result = {audioInfo.sampleRate/2/F_RES};
    result.amps = malloc(result.freqCount*sizeof(*result.amps));
    size_t i = 0;
    for (float f = F_RES; f <= audioInfo.sampleRate/2 ; f += F_RES, i++)
    {
        float x = 0;
        float y = 0;

        float delta = (2*M_PI*f)/audioInfo.sampleRate; //increase per sample
        float deltaSin = sinf(delta);
        float deltaCos = cosf(delta);
        float currSin = sinf(phase*2*M_PI*f);
        float currCos = cosf(phase*2*M_PI*f);
        float sinTemp;
        for (size_t j = 0; j < audioInfo.sampleCount; j++)
        {
            x += audioInfo.samples[j]*currSin;
            y += audioInfo.samples[j]*currCos;
            sinTemp = currSin;
            currSin = currSin*deltaCos + currCos*deltaSin;
            currCos = currCos*deltaCos - sinTemp*deltaSin;
        }
        result.amps[i] = sqrtf((x*x + y*y)) * (2.0f / audioInfo.sampleCount) ;
        //printf("%f - %f\n", f, result.amps[i]);
    }
    return result;
}
