#include "config.h"

typedef struct
{
    size_t sampleCount;
    float sampleRate;
    float sampleDuration;
    size_t reserved;
    float* samples;
} audioInfo_s;

typedef struct
{
    size_t freqCount;
    float* amps;
} freqDomainST_s;

typedef struct
{
    float freq[MAX_PEAKS];
    float amp[MAX_PEAKS];
    size_t count;
} signature_s;

typedef struct
{
    char name[MAX_NAME];
    float freq[MAX_PEAKS];
    size_t count;
} objectRecord_s;