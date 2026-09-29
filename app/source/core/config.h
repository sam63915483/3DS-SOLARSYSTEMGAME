#ifndef CONFIG_H
#define CONFIG_H

#define SR 32728                 /* NDSP native rate, also the mic rate */
#define BLOCK 512                /* mixer block size in frames */
#define MAX_REC_SECONDS 10
#define MAX_REC_SAMPLES (SR * MAX_REC_SECONDS)
#define ROOT_MIDI 60
#define WAVE_COLUMNS 368
#define TOP_W 400
#define TOP_H 240
#define BOT_W 320
#define BOT_H 240

#endif
