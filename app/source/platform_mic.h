/* Microphone capture into a recording, with the live waveform for the top screen. */
#ifndef PLATFORM_MIC_H
#define PLATFORM_MIC_H
#include "mixer.h"

int   mic_init(void);              /* 0 if the mic service is unavailable */
int   mic_start(float maxSeconds); /* begins capturing; 0 on failure */
void  mic_pump(Mixer *m);          /* call every frame; auto-stops at the length cap */
void  mic_stop(Mixer *m);          /* ends capture and installs the sample */
int   mic_is_recording(void);
int   mic_is_armed(void);          /* recording but still waiting for a sound to start the window */
unsigned mic_last_error(void);     /* libctru Result of the last failed mic call, 0 if none */
float mic_seconds(void);
float mic_max_seconds(void);
float mic_level(void);
const float *mic_live_min(void);   /* WAVE_COLUMNS entries over the full recording length */
const float *mic_live_max(void);
void  mic_exit(void);

#endif
