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
