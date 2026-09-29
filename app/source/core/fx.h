/* The five insert effects with presets. Port of mockup/effects.js.
 * Chain order: DISTORTION -> CHORUS -> FLANGER -> DELAY -> REVERB.
 * Mono in; the delay stage produces stereo, so the chain outputs L/R. */
#ifndef FX_H
#define FX_H
#include "config.h"
#include "dsp_util.h"

#define FX_COUNT 5
#define FX_PARAMS 4
#define FX_PRESETS 5

enum { FX_DISTORTION = 0, FX_CHORUS, FX_FLANGER, FX_DELAY, FX_REVERB };
enum { MODE_NORMAL = 0, MODE_MONO, MODE_PINGPONG, MODE_WOBBLE, MODE_GIANT };

typedef struct { const char *label; float min, max; int log; char unit; } FxParam;   /* unit: 's','m'(ms),'h'(Hz),'r'(rate Hz),'%' */
typedef struct { const char *name; int mode; float values[FX_PARAMS]; } FxPreset;
typedef struct {
    const char *name;
    int keepDry;                /* delay/reverb: dry stays at 1 and mix sets wet only */
    FxParam params[FX_PARAMS];
    FxPreset presets[FX_PRESETS];
} FxDef;

extern const FxDef FX_DEFS[FX_COUNT];

/* User-facing settings for one effect. */
typedef struct { int enabled; int preset; int mode; float values[FX_PARAMS]; } FxConfig;

/* Slider position 0..1 <-> value, and text formatting for the UI. */
float fx_to_norm(const FxParam *p, float v);
float fx_from_norm(const FxParam *p, float t);
void  fx_format_value(const FxParam *p, float v, char out[16]);
void  fx_config_default(int fx, FxConfig *c);
void  fx_config_load_preset(int fx, FxConfig *c, int preset);
int   fx_config_is_tweaked(int fx, const FxConfig *c);

/* ---- Audio state (large buffers, allocate once, statically) ---- */
#define CHORUS_MAX (SR / 5)        /* 200 ms */
#define FLANGER_MAX (SR / 20)      /* 50 ms */
#define DELAY_MAX (SR + 64)        /* 1 s */
#define REVERB_COMBS 4
#define REVERB_APS 2

typedef struct {
    /* distortion */
    float drive, crushLevels, distMix; OnePole tone;
    /* chorus */
    float chorusBuf[CHORUS_MAX]; DelayLine chorus; LFO chorusLfo; float chorusBase, chorusDepth, chorusMix;
    /* flanger */
    float flangerBuf[FLANGER_MAX]; DelayLine flanger; LFO flangerLfo; float flangerDepth, flangerFb, flangerMix;
    /* delay */
    float delayBufL[DELAY_MAX], delayBufR[DELAY_MAX]; DelayLine delayL, delayR; OnePole delayToneL, delayToneR;
    float delayTime, delayFb, delayMix; int delayMode; LFO delayLfo;
    /* reverb */
    float combBuf[REVERB_COMBS][1700]; float apBuf[REVERB_APS][600];
    DelayLine comb[REVERB_COMBS], ap[REVERB_APS]; float combStore[REVERB_COMBS];
    float revFeedback, revDamp, revMix; float preBuf[SR / 8]; DelayLine pre; float preTime;
    int enabled[FX_COUNT];
} FxState;

void fx_init(FxState *s);
void fx_clear(FxState *s);                       /* silence all tails (used when recording starts) */
void fx_apply(FxState *s, int fx, const FxConfig *c);
/* Processes n mono samples in-place into stereo outputs. */
void fx_process(FxState *s, const float *in, float *outL, float *outR, int n);

#endif
