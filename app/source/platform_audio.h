/* NDSP streaming of the software mixer's output. */
#ifndef PLATFORM_AUDIO_H
#define PLATFORM_AUDIO_H
#include "mixer.h"

int  audio_init(void);            /* 0 if the DSP could not be started (missing dspfirm.cdc) */
void audio_pump(Mixer *m);        /* call every frame: refills finished wave buffers */
void audio_exit(void);

#endif
