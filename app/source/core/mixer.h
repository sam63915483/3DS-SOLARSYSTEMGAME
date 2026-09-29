/* The software mixer: voices -> circle-pad filters -> effects -> stereo int16 blocks.
 * Owns the sample, the voices and the effect state. Pure C; the platform layer only
 * calls mixer_render() to fill NDSP buffers. */
#ifndef MIXER_H
#define MIXER_H
#include "config.h"
#include "sample.h"
#include "voice.h"
#include "fx.h"

#define MAX_VOICES 4

typedef struct {
    Sample sample;
    Voice voices[MAX_VOICES];
    unsigned age;
    int held;                   /* index of the voice under the finger, or -1 */
    int instrument;
    float bend, padX;
    SVF padLow, padHigh;
    FxState fx;
    float master, masterTarget; /* 0 while recording */
    /* for the visualizer: the last rendered mono block, its RMS, and the held note */
    float scope[BLOCK]; float scopeRms; int scopeMidi;
    int nanResets;
} Mixer;

void mixer_init(Mixer *m);
void mixer_note_on(Mixer *m, int midi);
void mixer_note_off(Mixer *m);
void mixer_set_instrument(Mixer *m, int instrument);
void mixer_set_pad(Mixer *m, float x, float y);      /* circle pad, each -1..1 */
void mixer_apply_fx(Mixer *m, int fx, const FxConfig *c);
void mixer_set_muted(Mixer *m, int muted);           /* mutes and clears effect tails */
/* Renders n frames of interleaved stereo int16. n must be <= BLOCK. */
void mixer_render(Mixer *m, short *out, int n);

#endif
