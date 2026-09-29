#include <3ds.h>
#include <string.h>
#include "platform_audio.h"

/* The mixer runs on its own thread, woken by NDSP every DSP frame, so small buffers can be
 * used without depending on the main loop's frame rate. 3 x 256 frames = 23 ms of queue. */
#define AUDIO_FRAMES 256
#define NBUF 3
static ndspWaveBuf wb[NBUF];
static s16 *pcm;
static int ok;
static Mixer *mixer;
static LightLock lock;
static LightEvent wake;
static Thread thread;
static volatile int quit;

static void fill_buffers(void) {
    for (int i = 0; i < NBUF; i++) {
        if (wb[i].status != NDSP_WBUF_DONE && wb[i].status != NDSP_WBUF_FREE) continue;
        s16 *buf = pcm + i * AUDIO_FRAMES * 2;
        LightLock_Lock(&lock);
        mixer_render(mixer, buf, AUDIO_FRAMES);
        LightLock_Unlock(&lock);
        DSP_FlushDataCache(buf, AUDIO_FRAMES * 2 * sizeof(s16));
        ndspChnWaveBufAdd(0, &wb[i]);
    }
}

static void ndsp_frame(void *unused) { (void)unused; LightEvent_Signal(&wake); }

static void audio_thread(void *arg) {
    (void)arg;
    while (!quit) {
        LightEvent_Wait(&wake);
        if (!quit) fill_buffers();
    }
}

int audio_init(Mixer *m) {
    mixer = m;
    if (R_FAILED(ndspInit())) { ok = 0; return 0; }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_NONE);
    ndspChnSetRate(0, (float)SR);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    float mix[12]; memset(mix, 0, sizeof(mix)); mix[0] = mix[1] = 1.0f;
    ndspChnSetMix(0, mix);
    pcm = linearAlloc(NBUF * AUDIO_FRAMES * 2 * sizeof(s16));
    memset(pcm, 0, NBUF * AUDIO_FRAMES * 2 * sizeof(s16));
    memset(wb, 0, sizeof(wb));
    for (int i = 0; i < NBUF; i++) {
        wb[i].data_vaddr = pcm + i * AUDIO_FRAMES * 2;
        wb[i].nsamples = AUDIO_FRAMES;
        wb[i].status = NDSP_WBUF_DONE;
    }
    LightLock_Init(&lock);
    LightEvent_Init(&wake, RESET_ONESHOT);
    quit = 0;
    ok = 1;
    fill_buffers();
    ndspSetCallback(ndsp_frame, NULL);
    thread = threadCreate(audio_thread, NULL, 32 * 1024, 0x19, -1, false);   /* above the main thread (0x30) */
    return 1;
}

void audio_lock(void) { if (ok) LightLock_Lock(&lock); }
void audio_unlock(void) { if (ok) LightLock_Unlock(&lock); }

void audio_exit(void) {
    if (!ok) return;
    quit = 1;
    LightEvent_Signal(&wake);
    threadJoin(thread, U64_MAX);
    threadFree(thread);
    ndspSetCallback(NULL, NULL);
    ndspChnReset(0);
    ndspExit();
    linearFree(pcm);
    ok = 0;
}
