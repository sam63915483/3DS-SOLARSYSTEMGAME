/* NDSP streaming of the software mixer's output, on a dedicated audio thread. */
#ifndef PLATFORM_AUDIO_H
#define PLATFORM_AUDIO_H
#include "mixer.h"

int  audio_init(Mixer *m);        /* 0 if the DSP could not be started (missing dspfirm.cdc) */
/* Anything that changes the mixer from the main thread must hold this lock. */
void audio_lock(void);
void audio_unlock(void);
void audio_exit(void);

#endif
