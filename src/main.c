#define _USE_MATH_DEFINES
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <Windows.h>
#include <synchapi.h>
#include "api.h"

#include "miniaudio.h"

//GLOBAL VARS
ma_device device;
audioInfo_s audioInfo = {0, SAMPLE_RATE, 0, SAMPLE_RATE*TAP_DURATION};

void samplesAppend(audioInfo_s* audioInfo, float* toAppend, size_t count)
{
    if (audioInfo->sampleCount + count > audioInfo->reserved)
    {
        while (audioInfo->sampleCount + count > audioInfo->reserved) 
            audioInfo->reserved *= 2;
        audioInfo->samples = realloc(audioInfo->samples, audioInfo->reserved*sizeof(*audioInfo->samples));
    }
    memcpy(audioInfo->samples + audioInfo->sampleCount, toAppend, count*sizeof(*audioInfo->samples));
    audioInfo->sampleCount += count;
    audioInfo->sampleDuration += (float)count/(float)audioInfo->sampleRate;
}

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    if (pInput == NULL) 
    {
        return;
    }
    samplesAppend(&audioInfo, (float*)pInput, frameCount);
}

int initCaptureDevice()
{
    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_f32;
    config.capture.channels = 1;
    config.sampleRate = SAMPLE_RATE;
    config.dataCallback = data_callback;
    config.periodSizeInMilliseconds = 20;

    if (ma_device_init(NULL, &config, &device) != MA_SUCCESS)
    {
        return -1;
    }
    
    return 0;
}

size_t findStart(const float *samples, size_t count, float thresholdRatio)
{
    float peak = 0.0f;
    for (size_t i = 0; i < count; i++) 
    {
        float mag = fabsf(samples[i]);
        if (mag > peak) peak = mag;
    }
    float threshold = peak * thresholdRatio;

    for (size_t i = 0; i < count; i++) 
    {
        if (fabsf(samples[i]) >= threshold) 
            return i;
    }
    return 0; // no clear start
}

float* extractTapWindow(const audioInfo_s* audioInfo, size_t winLen /*estimated duration of tap * sampleRate*/, size_t* finalLen)
{
    size_t onset = findStart(audioInfo->samples, audioInfo->sampleCount, TR);
    size_t skipSamples = (size_t)(0.003 * SAMPLE_RATE); // 3ms (the transient state)
    size_t start = onset + skipSamples;

    if (start >= audioInfo->sampleCount) 
    {
        *finalLen = 0;
        return NULL;
    }
    
    if (start + winLen > audioInfo->sampleCount)
    {
        winLen = audioInfo->sampleCount - start;
    }

    float *window = malloc(winLen * sizeof(float));

    double mean = 0;
    for (size_t i = 0; i < winLen; i++)
    {
        mean += audioInfo->samples[start + i];
    }
    mean /= (double)winLen;

    for (size_t i = 0; i < winLen; i++) 
    {
        float centered = (float)(audioInfo->samples[start + i] - mean);
        float hann = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (winLen - 1));
        window[i] = centered * hann;
    }
    *finalLen = winLen;
    return window;
}


int main()
{
    
    audioInfo.samples = malloc(audioInfo.reserved*sizeof(*audioInfo.samples));
    initCaptureDevice();
    {   
        size_t i = 3;
        printf("starting recording in %zu sec...", i);
        for (; i > 0; i--) 
        {
            printf("%zu...", i);
            Sleep(1000);
        }
        printf("recording...");
    }
    ma_device_start(&device);

    while ('s' != getchar()) 
    {
        Sleep(50);
    }
    
    ma_device_uninit(&device);
    printf("done\n");

    audioInfo_s ftPass;
    ftPass.reserved = 0; //not relevant
    ftPass.sampleRate = audioInfo.sampleRate;
    ftPass.sampleCount = TAP_DURATION*audioInfo.sampleRate;
    ftPass.samples = extractTapWindow(&audioInfo, ftPass.sampleCount, &ftPass.sampleCount);
    ftPass.sampleDuration = (float)audioInfo.sampleCount/(float)audioInfo.sampleDuration;

    free(audioInfo.samples);
    
    printf("starting ft...");
    freqDomainST_s fd = dft(ftPass, 0);
    printf("done\n");

    free(ftPass.samples);

    float peak = 0;
    for (size_t i = FILTER/F_RES; i < fd.freqCount; i++)
    {
        if (fd.amps[i] > peak) 
        {
            peak = fd.amps[i];
        }
    }

    float threshold = 0.1*peak;
    
    for (size_t i = FILTER/F_RES; i < fd.freqCount; i++)
    {
        if (fd.amps[i] > threshold) 
        {
            printf("%f -> %f\n", (float)(i+1)*(F_RES), fd.amps[i]);
        }
    }
    free(fd.amps);
}