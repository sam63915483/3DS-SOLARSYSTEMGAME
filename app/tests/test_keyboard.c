#include "minitest.h"
#include "keyboard.h"

int main(void) {
    Keyboard kb; kb_init(&kb);
    KeyRect rects[128]; int n;
    Rect area = {0, 48, 320, 156};
    char name[8];

    CHECK(kb.whiteCount == 36, "C2..C7 has 36 white keys");
    CHECK(kb_visible_white(&kb) == 14, "default zoom shows 14 white keys");
    kb_note_name(60, name); CHECK(name[0]=='C' && name[1]=='4' && name[2]==0, "C4 name");
    kb_note_name(61, name); CHECK(name[0]=='C' && name[1]=='#' && name[2]=='4', "C#4 name");
    kb_note_name(36, name); CHECK(name[0]=='C' && name[1]=='2', "C2 name");

    n = kb_layout(&kb, area, rects, 128);
    int whites = 0, blacks = 0, width = 0;
    for (int i = 0; i < n; i++) { if (rects[i].black) blacks++; else { whites++; width += rects[i].r.w; } }
    CHECK(whites == 14 && width == 320, "14 white rects tile 320px exactly");
    CHECK(blacks == 10, "10 black keys over two octaves");
    CHECK(kb_hit(&kb, area, 5, 200) == 48, "bottom-left hit is C3");
    CHECK(kb_hit(&kb, area, 5, 20) == -1, "outside area is -1");
    int csharp = -1;
    for (int i = 0; i < n; i++) if (rects[i].midi == 61) csharp = i;
    CHECK(csharp >= 0 && kb_hit(&kb, area, rects[csharp].r.x + 1, 60) == 61, "hit on black key returns C#4");
    CHECK(kb_hit(&kb, area, rects[csharp].r.x + 1, 190) != 61, "below black key hits white");

    int lo, hi;
    kb_visible_range(&kb, &lo, &hi);
    CHECK(lo == 48 && hi == 71, "default view C3..B4");
    kb_zoom_in(&kb);
    kb_visible_range(&kb, &lo, &hi);
    CHECK(kb_visible_white(&kb) == 10 && lo == 52 && hi == 67, "zoom in keeps centre (E3..G4)");
    for (int i = 0; i < 10; i++) kb_zoom_in(&kb);
    CHECK(kb_visible_white(&kb) == 5, "zoom in clamps at 5");
    for (int i = 0; i < 10; i++) kb_zoom_out(&kb);
    CHECK(kb_visible_white(&kb) == 36 && kb.scroll == 0 && kb_max_scroll(&kb) == 0, "fully zoomed out");
    n = kb_layout(&kb, area, rects, 128);
    int minw = 999;
    for (int i = 0; i < n; i++) if (!rects[i].black && rects[i].r.w < minw) minw = rects[i].r.w;
    CHECK(minw >= 8, "fully zoomed out keys still >= 8px wide");
    kb_zoom_in(&kb); kb_set_scroll(&kb, 999); CHECK(kb.scroll == kb_max_scroll(&kb), "scroll clamps high");
    kb_set_scroll(&kb, -5); CHECK(kb.scroll == 0, "scroll clamps low");
    MT_END();
}
