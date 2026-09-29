/* Keyboard model: key range, zoom, scroll, layout, hit-testing. Port of mockup/keyboard.js.
 * Pure C, no 3DS headers. All coordinates are native bottom-screen pixels (320x240). */
#ifndef KEYBOARD_H
#define KEYBOARD_H

#define LOW_MIDI 36
#define HIGH_MIDI 96
#define KEY_COUNT (HIGH_MIDI - LOW_MIDI + 1)
#define ZOOM_LEVELS 7

typedef struct { int x, y, w, h; } Rect;
typedef struct { int midi; int black; Rect r; } KeyRect;

typedef struct {
    int whiteCount;
    int zoomIndex;
    int scroll;                 /* leftmost visible white key index */
    int whiteIndex[KEY_COUNT];  /* for black keys: the white key to their left */
    int isBlack[KEY_COUNT];
} Keyboard;

void kb_init(Keyboard *kb);
int  kb_visible_white(const Keyboard *kb);
int  kb_max_scroll(const Keyboard *kb);
void kb_set_scroll(Keyboard *kb, int s);
void kb_zoom_in(Keyboard *kb);   /* fewer, bigger keys */
void kb_zoom_out(Keyboard *kb);  /* more, smaller keys */
void kb_visible_range(const Keyboard *kb, int *lo, int *hi);
/* Fills out[] with white keys first, then black keys on top. Returns the count. */
int  kb_layout(const Keyboard *kb, Rect area, KeyRect *out, int max);
/* Midi number of the key under (px,py), or -1. */
int  kb_hit(const Keyboard *kb, Rect area, int px, int py);
void kb_note_name(int midi, char out[8]);
int  rect_contains(Rect r, int px, int py);

#endif
