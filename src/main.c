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
float F_RES = 0;

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

signature_s extractSignature(freqDomainST_s fd, float threshold, size_t startIdx)
{
    signature_s sig = {0};

    for (size_t i = startIdx + 1; i + 1 < fd.freqCount; i++)
    {
        if (fd.amps[i] <= threshold) continue;
        if (fd.amps[i] <= fd.amps[i - 1] || fd.amps[i] <= fd.amps[i + 1]) continue; // not a true local max

        float freq = (float)(i + 1) * F_RES;
        float amp  = fd.amps[i];

        if (sig.count < MAX_PEAKS)
        {
            sig.freq[sig.count] = freq;
            sig.amp[sig.count]  = amp;
            sig.count++;
        }
        else
        {
            size_t weakest = 0;
            for (size_t k = 1; k < MAX_PEAKS; k++)
                if (sig.amp[k] < sig.amp[weakest]) weakest = k;
            if (amp > sig.amp[weakest]) { sig.freq[weakest] = freq; sig.amp[weakest] = amp; }
        }
    }
    return sig;
}

void appendSample(const char* name, signature_s sig)
{
    char path[64];
    snprintf(path, sizeof(path), "%s%s.bin", DATA_DIR, name);
    FILE* f = fopen(path, "ab");
    if (!f) 
    { 
        printf("failed to open %s\n", path); return; 
    }
    fwrite(&sig, sizeof(signature_s), 1, f);
    fclose(f);
}

typedef struct
{
    float freq;
    float amp;
    size_t sampleId;
} peakPoint_s;

typedef struct
{
    float freq;
    float avgAmp;
    size_t support;
} peakCluster_s;


int comparePeakPoints(const void* a, const void* b)
{
    const peakPoint_s* pa = (const peakPoint_s*)a;
    const peakPoint_s* pb = (const peakPoint_s*)b;

    if (pa->freq < pb->freq) return -1;
    if (pa->freq > pb->freq) return 1;
    return 0;
}


int compareClustersByStrength(const void* a, const void* b)
{
    const peakCluster_s* ca = (const peakCluster_s*)a;
    const peakCluster_s* cb = (const peakCluster_s*)b;

    // First prefer clusters seen in more taps.
    if (ca->support > cb->support) return -1;
    if (ca->support < cb->support) return 1;

    // Then prefer stronger peaks.
    if (ca->avgAmp > cb->avgAmp) return -1;
    if (ca->avgAmp < cb->avgAmp) return 1;

    return 0;
}


int compareFreqs(const void* a, const void* b)
{
    const float* fa = (const float*)a;
    const float* fb = (const float*)b;

    if (*fa < *fb) return -1;
    if (*fa > *fb) return 1;
    return 0;
}


/*
    Returns how many different recordings contributed peaks
    to [begin, end).

    This matters because one recording should not get counted
    multiple times just because it happened to produce two
    nearby peaks.
*/
size_t clusterSupport(const peakPoint_s* points, size_t begin, size_t end)
{
    size_t support = 0;

    for (size_t i = begin; i < end; i++)
    {
        int alreadySeen = 0;

        for (size_t j = begin; j < i; j++)
        {
            if (points[j].sampleId == points[i].sampleId)
            {
                alreadySeen = 1;
                break;
            }
        }

        if (!alreadySeen)
            support++;
    }

    return support;
}


objectRecord_s computeAveragedSignature(const char* name)
{
    objectRecord_s rec = {0};

    strncpy(rec.name, name, MAX_NAME - 1);
    rec.name[MAX_NAME - 1] = '\0';

    char path[64];
    snprintf(path, sizeof(path), "%s%s.bin", DATA_DIR, name);

    FILE* f = fopen(path, "rb");
    if (!f)
    {
        return rec;
    }

    /*
        Read every peak from every recording into one array.
    */
    peakPoint_s* points = NULL;

    size_t pointCount = 0;
    size_t pointCapacity = 0;
    size_t sampleCount = 0;

    signature_s sample;

    while (fread(&sample, sizeof(signature_s), 1, f) == 1)
    {
        if (sample.count > MAX_PEAKS)
            sample.count = MAX_PEAKS;

        for (size_t k = 0; k < sample.count; k++)
        {
            /*
                Grow array when necessary.
            */
            if (pointCount >= pointCapacity)
            {
                size_t newCapacity = (pointCapacity == 0) ? 64 : pointCapacity * 2;

                peakPoint_s* newPoints = realloc(points, newCapacity * sizeof(*points));

                if (!newPoints)
                {
                    free(points);
                    fclose(f);

                    printf("out of memory while building database\n");
                    return rec;
                }

                points = newPoints;
                pointCapacity = newCapacity;
            }

            points[pointCount].freq = sample.freq[k];
            points[pointCount].amp = sample.amp[k];
            points[pointCount].sampleId = sampleCount;

            pointCount++;
        }

        sampleCount++;
    }

    fclose(f);

    if (pointCount == 0 || sampleCount == 0)
    {
        free(points);
        return rec;
    }

    /*
        Sort all peaks by frequency.
    */
    qsort(
        points,
        pointCount,
        sizeof(*points),
        comparePeakPoints
    );

    /*
        A cluster must appear in at least half of the recordings.

        Examples:
            1 recording  -> 1
            2 recordings -> 1
            3 recordings -> 2
            4 recordings -> 2
            5 recordings -> 3
            ...
    */
    size_t minSupport = (size_t)ceilf((float)sampleCount * DB_MIN_SUPPORT_RATIO);

    if (minSupport < 1)
        minSupport = 1;

    /*
        Build frequency clusters.
    */
    peakCluster_s* clusters =
        malloc(pointCount * sizeof(*clusters));

    if (!clusters)
    {
        free(points);
        printf("out of memory while building clusters\n");
        return rec;
    }

    size_t clusterCount = 0;

    size_t i = 0;

    while (i < pointCount)
    {
        size_t begin = i;
        size_t end = i + 1;

        double freqSum = points[i].freq;
        double ampSum = points[i].amp;

        /*
            Keep adding points while they remain close to
            the current cluster mean.
        */
        while (end < pointCount)
        {
            float currentMean =
                (float)(freqSum / (double)(end - begin));

            float diff =
                fabsf(points[end].freq - currentMean);

            if (diff > DB_CLUSTER_TOLERANCE_HZ)
                break;

            freqSum += points[end].freq;
            ampSum += points[end].amp;

            end++;
        }

        size_t support =
            clusterSupport(points, begin, end);

        if (support >= minSupport)
        {
            clusters[clusterCount].freq =
                (float)(freqSum / (double)(end - begin));

            clusters[clusterCount].avgAmp =
                (float)(ampSum / (double)(end - begin));

            clusters[clusterCount].support = support;

            clusterCount++;
        }

        i = end;
    }

    free(points);

    if (clusterCount == 0)
    {
        free(clusters);
        return rec;
    }

    /*
        Strongest / most reliable clusters first.
    */
    qsort(
        clusters,
        clusterCount,
        sizeof(*clusters),
        compareClustersByStrength
    );

    /*
        Keep only MAX_PEAKS clusters.
    */
    size_t keepCount = (clusterCount < MAX_PEAKS)? clusterCount: MAX_PEAKS;

    for (size_t k = 0; k < keepCount; k++)
    {
        rec.freq[k] = clusters[k].freq;
    }

    rec.count = keepCount;

    /*
        Sort final signature by frequency.
    */
    qsort(
        rec.freq,
        rec.count,
        sizeof(rec.freq[0]),
        compareFreqs
    );

    free(clusters);

    return rec;
}

void upsertObjectRecord(objectRecord_s rec)
{
    char path[64];
    snprintf(path, sizeof(path), "%sobjects.bin", DATA_DIR);

    FILE* f = fopen(path, "r+b");
    objectRecord_s existing;
    int found = 0;
    if (f)
    {
        while (fread(&existing, sizeof(objectRecord_s), 1, f) == 1)
        {
            if (strncmp(existing.name, rec.name, MAX_NAME) == 0)
            {
                fseek(f, -(long)sizeof(objectRecord_s), SEEK_CUR);
                fwrite(&rec, sizeof(objectRecord_s), 1, f);
                found = 1;
                break;
            }
        }
        fclose(f);
    }
    if (!found)
    {
        f = fopen(path, "ab");
        if (!f) { printf("failed to open %s\n", path); return; }
        fwrite(&rec, sizeof(objectRecord_s), 1, f);
        fclose(f);
    }
}

typedef struct
{
    size_t matched;
    float totalError;
} matchState_s;


int betterMatch(matchState_s a, matchState_s b)
{
    if (a.matched != b.matched)
        return a.matched > b.matched;

    return a.totalError < b.totalError;
}


matchState_s solveMatch(
    const float* liveFreq,
    size_t liveCount,
    const float* storedFreq,
    size_t storedCount,
    size_t i,
    size_t j,
    matchState_s memo[MAX_PEAKS + 1][MAX_PEAKS + 1],
    int visited[MAX_PEAKS + 1][MAX_PEAKS + 1])
{
    if (i >= liveCount || j >= storedCount)
    {
        matchState_s result = {0, 0.0f};
        return result;
    }

    if (visited[i][j])
        return memo[i][j];

    visited[i][j] = 1;

    /*
        Option 1:
        Ignore current live peak.
    */
    matchState_s best =
        solveMatch(
            liveFreq,
            liveCount,
            storedFreq,
            storedCount,
            i + 1,
            j,
            memo,
            visited
        );

    /*
        Option 2:
        Ignore current stored peak.
    */
    matchState_s skipStored =
        solveMatch(
            liveFreq,
            liveCount,
            storedFreq,
            storedCount,
            i,
            j + 1,
            memo,
            visited
        );

    if (betterMatch(skipStored, best))
        best = skipStored;

    /*
        Option 3:
        Match the two peaks.
    */
    float diff =
        fabsf(liveFreq[i] - storedFreq[j]);

    if (diff <= MATCH_TOLERANCE_HZ)
    {
        matchState_s matched =solveMatch(liveFreq, liveCount, storedFreq, storedCount, i + 1, j + 1, memo, visited);

        matched.matched++;
        matched.totalError += diff;

        if (betterMatch(matched, best))
            best = matched;
    }

    memo[i][j] = best;
    return best;
}


float scoreMatch(signature_s live, objectRecord_s stored)
{
    if (live.count == 0 || stored.count == 0)
        return 1e9f;

    float liveFreq[MAX_PEAKS];
    float storedFreq[MAX_PEAKS];

    size_t liveCount = (live.count < MAX_PEAKS)? live.count: MAX_PEAKS;

    size_t storedCount = (stored.count < MAX_PEAKS)? stored.count: MAX_PEAKS;

    for (size_t i = 0; i < liveCount; i++)
        liveFreq[i] = live.freq[i];

    for (size_t i = 0; i < storedCount; i++)
        storedFreq[i] = stored.freq[i];

    qsort(
        liveFreq,
        liveCount,
        sizeof(liveFreq[0]),
        compareFreqs
    );

    qsort(
        storedFreq,
        storedCount,
        sizeof(storedFreq[0]),
        compareFreqs
    );

    matchState_s memo[MAX_PEAKS + 1][MAX_PEAKS + 1] = {0};
    int visited[MAX_PEAKS + 1][MAX_PEAKS + 1] = {0};

    matchState_s result =
        solveMatch(
            liveFreq,
            liveCount,
            storedFreq,
            storedCount,
            0,
            0,
            memo,
            visited
        );

    if (result.matched == 0)
        return 1e9f;

    float averageError = result.totalError / (float)result.matched;

    float coverage = (float)result.matched / (float)liveCount;

    return averageError / coverage;
}

void identifyObject(signature_s live)
{
    char path[64];
    snprintf(path, sizeof(path), "%sobjects.bin", DATA_DIR);

    FILE* f = fopen(path, "rb");

    if (!f)
    {
        printf("no objects recorded yet\n");
        return;
    }

    objectRecord_s rec;

    char bestName[MAX_NAME] = "none";

    float bestScore = 1e9f;
    float secondBestScore = 1e9f;

    while (fread(&rec, sizeof(objectRecord_s), 1, f) == 1)
    {
        if (rec.count == 0)
            continue;

        float score = scoreMatch(live, rec);
        printf("%-20s score=%.3f\n", rec.name, score);

        if (score < bestScore)
        {
            secondBestScore = bestScore;
            bestScore = score;

            strncpy(bestName, rec.name, MAX_NAME - 1);
            bestName[MAX_NAME - 1] = '\0';
        }
        else if (score < secondBestScore)
        {
            secondBestScore = score;
        }
    }

    fclose(f);

    if (bestScore >= 1e8f)
    {
        printf("RESULT:unknown:0.00\n");
        return;
    }

    float similarity = 1.0f / (1.0f + bestScore);

    printf("Best Match: %s",bestName);
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
        printf("recording...(press s an enter to finish)");
    }
    ma_device_start(&device);

    while ('s' != getchar()) 
    {
        Sleep(50);
    }
    while (getchar() != '\n') {}
    
    ma_device_uninit(&device);
    printf("done\n");

    audioInfo_s ftPass;
    ftPass.reserved = 0; //not relevant
    ftPass.sampleRate = audioInfo.sampleRate;
    ftPass.sampleCount = TAP_DURATION*audioInfo.sampleRate;
    ftPass.samples = extractTapWindow(&audioInfo, ftPass.sampleCount, &ftPass.sampleCount);
    if (ftPass.samples == NULL || ftPass.sampleCount < 2)
    {
        printf("Could not detect a valid tap.\n");
        free(audioInfo.samples);
        return 1;
    }
    ftPass.sampleDuration = (float)ftPass.sampleCount/(float)ftPass.sampleRate;\
    F_RES = 1/ftPass.sampleDuration;

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
        
    float threshold = 0.2f*peak;
    
    size_t startIdx = FILTER/F_RES;
    signature_s sig = extractSignature(fd, threshold, startIdx);
    
    CreateDirectoryA(DATA_DIR, NULL);
    
    printf("identify (i) or add (a) -\n");
    char mode = getchar();
    
    if (mode == 'i') 
    {
        identifyObject(sig);
    }

    else if (mode == 'a')
    {
        char name[MAX_NAME];
        printf("enter object (%d char limit) - \n", MAX_NAME - 1);
        scanf(" %29s", name);
    
        appendSample(name, sig);
        objectRecord_s rec = computeAveragedSignature(name);
        upsertObjectRecord(rec);
        printf("saved sample for %s\n", name);
    }
    
    free(fd.amps);
}