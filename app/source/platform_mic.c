#include <3ds.h>
#include <malloc.h>
#include <math.h>
#include <string.h>
#include "platform_mic.h"
#include "platform_audio.h"

/* After REC the recorder arms: it discards the speaker tail, then waits until the input
 * exceeds the trim floor. Only then does the length window start (with 10 ms of pre-roll),
 * so a short SK-1 style take always gets its full length from the moment the sound begins. */
#define MICBUF_SIZE 0x10000        /* ~1 s ring buffer; drained every frame */
#define DISCARD_SAMPLES (SR / 5)   /* 200 ms: speaker tail + mute fade */
#define PREROLL (SR / 100)         /* 10 ms window for the trigger and the pre-roll */
#define TRIGGER_RMS 0.04f          /* same floor as the trimmer (about -28 dBFS) */
#define ARM_TIMEOUT (SR * 15)      /* give up after 15 s of silence */
static u8 *micbuf;
static u32 dataSize, readPos;
static int ok, recording, parity, discard, armed, armedFor;
static int live;                   /* 0 until the mic's write offset is seen moving */
static u32 startOffset;            /* offset reported right after start (may be stale) */
static u8 stage[2];
static float rec[MAX_REC_SAMPLES];
static int recLen, maxSamples = MAX_REC_SAMPLES;
static float pre[PREROLL]; static int preW, preN; static double preSq;
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

int mic_start(float maxSeconds) {
    if (!ok || recording) return 0;
    maxSamples = (int)(maxSeconds * SR);
    if (maxSamples > MAX_REC_SAMPLES) maxSamples = MAX_REC_SAMPLES;
    if (maxSamples < SR / 10) maxSamples = SR / 10;
    readPos = 0; recLen = 0; parity = 0; level = 0; discard = DISCARD_SAMPLES;
    armed = 1; armedFor = 0; preW = 0; preN = 0; preSq = 0;
    memset(liveMin, 0, sizeof(liveMin)); memset(liveMax, 0, sizeof(liveMax));
    memset(micbuf, 0, MICBUF_SIZE);   /* no stale audio from the previous take */
    MICU_SetPower(true);
    Result r = MICU_StartSampling(MICU_ENCODING_PCM16_SIGNED, MICU_SAMPLE_RATE_32730, 0, dataSize, true);
    if (R_FAILED(r)) { lastError = r; return 0; }
    lastError = 0;
    /* The reported write offset can still be the previous take's until the mic really starts.
     * Wait for it to move, then begin reading from where the new audio actually starts. */
    startOffset = micGetLastSampleOffset();
    live = 0;
    recording = 1;
    return 1;
}

static void record_sample(float f, double *sq, int *n) {
    const float perCol = (float)maxSamples / WAVE_COLUMNS;
    int c = (int)(recLen / perCol); if (c >= WAVE_COLUMNS) c = WAVE_COLUMNS - 1;
    if (f < liveMin[c]) liveMin[c] = f;
    if (f > liveMax[c]) liveMax[c] = f;
    rec[recLen++] = f;
    *sq += f * f; (*n)++;
}

void mic_pump(Mixer *m) {
    if (!recording) return;
    u32 pos = micGetLastSampleOffset();
    if (!live) {
        if (pos == startOffset) return;
        live = 1;
        readPos = pos < startOffset ? 0 : startOffset;   /* restarted from 0, or continued */
    }
    double sq = 0; int n = 0;
    while (readPos != pos) {
        stage[parity++] = micbuf[readPos];
        readPos = (readPos + 1) % dataSize;
        if (parity < 2) continue;
        parity = 0;
        s16 v = (s16)(stage[0] | (stage[1] << 8));
        if (discard > 0) { discard--; continue; }   /* the speaker is still fading out */
        float f = v / 32768.0f;
        if (armed) {
            /* keep a 10 ms ring; trigger when its RMS crosses the floor */
            if (preN == PREROLL) preSq -= pre[preW] * pre[preW];
            pre[preW] = f; preW = (preW + 1) % PREROLL; if (preN < PREROLL) preN++;
            preSq += f * f; if (preSq < 0) preSq = 0;
            level = sqrtf((float)(preSq / preN));
            if (++armedFor > ARM_TIMEOUT) { recLen = 0; mic_stop(m); return; }
            if (preN < PREROLL || level < TRIGGER_RMS) continue;
            armed = 0;
            for (int i = 0; i < PREROLL; i++) record_sample(pre[(preW + i) % PREROLL], &sq, &n);
            continue;
        }
        record_sample(f, &sq, &n);
        if (recLen >= maxSamples) { mic_stop(m); return; }
    }
    if (n > 0) level = sqrtf((float)(sq / n));
}

void mic_stop(Mixer *m) {
    if (!recording) return;
    recording = 0; armed = 0;
    MICU_StopSampling();
    MICU_SetPower(false);
    if (recLen > SR / 50) { audio_lock(); mixer_set_recording(m, rec, recLen); audio_unlock(); }
}

int mic_is_recording(void) { return recording; }
int mic_is_armed(void) { return recording && armed; }
float mic_seconds(void) { return (float)recLen / SR; }
float mic_max_seconds(void) { return (float)maxSamples / SR; }
float mic_level(void) { return level; }
const float *mic_live_min(void) { return liveMin; }
const float *mic_live_max(void) { return liveMax; }

void mic_exit(void) {
    if (recording) { MICU_StopSampling(); recording = 0; }
    if (ok) micExit();
    free(micbuf);
}
