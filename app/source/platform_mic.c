#include <3ds.h>
#include <malloc.h>
#include <math.h>
#include <string.h>
#include "platform_mic.h"
#include "sample.h"

#define MICBUF_SIZE 0x10000        /* ~1 s ring buffer; drained every frame */
static u8 *micbuf;
static u32 dataSize, readPos;
static int ok, recording, parity;
static u8 stage[2];
static float rec[MAX_REC_SAMPLES];
static int recLen;
static float liveMin[WAVE_COLUMNS], liveMax[WAVE_COLUMNS];
static float level;
static Result lastError;

unsigned mic_last_error(void) { return (unsigned)lastError; }

int mic_init(void) {
    micbuf = memalign(0x1000, MICBUF_SIZE);
    if (!micbuf) return 0;
    Result r = micInit(micbuf, MICBUF_SIZE);
    if (R_FAILED(r)) { lastError = r; ok = 0; return 0; }
    dataSize = micGetSampleDataSize();
    MICU_SetGain(50);
    ok = 1;
    return 1;
}

int mic_start(void) {
    if (!ok || recording) return 0;
    readPos = 0; recLen = 0; parity = 0; level = 0;
    memset(liveMin, 0, sizeof(liveMin)); memset(liveMax, 0, sizeof(liveMax));
    MICU_SetPower(true);
    Result r = MICU_StartSampling(MICU_ENCODING_PCM16_SIGNED, MICU_SAMPLE_RATE_32730, 0, dataSize, true);
    if (R_FAILED(r)) { lastError = r; return 0; }
    lastError = 0;
    recording = 1;
    return 1;
}

void mic_pump(Mixer *m) {
    if (!recording) return;
    u32 pos = micGetLastSampleOffset();
    double sq = 0; int n = 0;
    const float perCol = (float)MAX_REC_SAMPLES / WAVE_COLUMNS;
    while (readPos != pos) {
        stage[parity++] = micbuf[readPos];
        readPos = (readPos + 1) % dataSize;
        if (parity < 2) continue;
        parity = 0;
        s16 v = (s16)(stage[0] | (stage[1] << 8));
        float f = v / 32768.0f;
        int c = (int)(recLen / perCol); if (c >= WAVE_COLUMNS) c = WAVE_COLUMNS - 1;
        if (f < liveMin[c]) liveMin[c] = f;
        if (f > liveMax[c]) liveMax[c] = f;
        rec[recLen++] = f;
        sq += f * f; n++;
        if (recLen >= MAX_REC_SAMPLES) { mic_stop(m); return; }
    }
    if (n > 0) level = sqrtf((float)(sq / n));
}

void mic_stop(Mixer *m) {
    if (!recording) return;
    recording = 0;
    MICU_StopSampling();
    MICU_SetPower(false);
    if (recLen > SR / 50) sample_from_recording(&m->sample, rec, recLen);
}

int mic_is_recording(void) { return recording; }
float mic_seconds(void) { return (float)recLen / SR; }
float mic_level(void) { return level; }
const float *mic_live_min(void) { return liveMin; }
const float *mic_live_max(void) { return liveMax; }

void mic_exit(void) {
    if (recording) { MICU_StopSampling(); recording = 0; }
    if (ok) micExit();
    free(micbuf);
}
