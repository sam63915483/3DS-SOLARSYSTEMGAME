#include <3ds.h>
#include <string.h>
#include "platform_audio.h"

#define NBUF 4                         /* 4 x 512 frames = 63 ms of buffering */
static ndspWaveBuf wb[NBUF];
static s16 *pcm;
static int ok;

int audio_init(void) {
    if (R_FAILED(ndspInit())) { ok = 0; return 0; }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_NONE);
    ndspChnSetRate(0, (float)SR);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    float mix[12]; memset(mix, 0, sizeof(mix)); mix[0] = mix[1] = 1.0f;
    ndspChnSetMix(0, mix);
    pcm = linearAlloc(NBUF * BLOCK * 2 * sizeof(s16));
    memset(pcm, 0, NBUF * BLOCK * 2 * sizeof(s16));
    memset(wb, 0, sizeof(wb));
    for (int i = 0; i < NBUF; i++) {
        wb[i].data_vaddr = pcm + i * BLOCK * 2;
        wb[i].nsamples = BLOCK;
        wb[i].status = NDSP_WBUF_DONE;
    }
    ok = 1;
    return 1;
}

void audio_pump(Mixer *m) {
    if (!ok) return;
    for (int i = 0; i < NBUF; i++) {
        if (wb[i].status != NDSP_WBUF_DONE && wb[i].status != NDSP_WBUF_FREE) continue;
        s16 *buf = pcm + i * BLOCK * 2;
        mixer_render(m, buf, BLOCK);
        DSP_FlushDataCache(buf, BLOCK * 2 * sizeof(s16));
        ndspChnWaveBufAdd(0, &wb[i]);
    }
}

void audio_exit(void) {
    if (!ok) return;
    ndspChnReset(0);
    ndspExit();
    linearFree(pcm);
}
