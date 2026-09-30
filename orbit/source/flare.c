#include "flare.h"

static u32 col(int r, int g, int b, float a) {
    if (a < 0) a = 0;
    if (a > 1) a = 1;
    return C2D_Color32(r, g, b, (u8)(a * 255.0f));
}

void flare_draw(C3D_RenderTarget *top, float sx, float sy, float k) {
    if (k <= 0.01f) return;
    C2D_SceneBegin(top);
    /* additive, weighted by alpha, so overlapping discs brighten instead of covering */
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_SRC_ALPHA, GPU_ONE);

    /* soft glow around the sun: many faint rings so the falloff reads as smooth */
    for (int i = 0; i < 14; i++) {
        float f = 1.0f - i / 14.0f;           /* 1 at the core, 0 at the edge */
        float r = 10.0f + i * 7.0f;
        C2D_DrawCircleSolid(sx, sy, 0, r, col(255, 228, 170, k * 0.07f * f * f));
    }
    /* horizontal streak */
    C2D_DrawRectSolid(sx - 170, sy - 1.0f, 0, 340, 2, col(255, 225, 180, k * 0.18f));
    C2D_DrawRectSolid(sx - 90, sy - 3.0f, 0, 180, 6, col(255, 225, 180, k * 0.08f));

    /* ghost discs along the line through the screen centre */
    const float cx = 200.0f, cy = 120.0f;
    float dx = cx - sx, dy = cy - sy;
    static const float t[6] = { 0.30f, 0.55f, 0.85f, 1.15f, 1.45f, 1.75f };
    static const float rad[6] = { 9, 16, 6, 22, 11, 30 };
    static const int rgb[6][3] = { {255,200,120}, {140,200,255}, {255,255,255}, {120,255,180}, {255,150,200}, {150,180,255} };
    static const float al[6] = { 0.10f, 0.08f, 0.14f, 0.06f, 0.09f, 0.05f };
    for (int i = 0; i < 6; i++)
        C2D_DrawCircleSolid(sx + dx * t[i], sy + dy * t[i], 0, rad[i], col(rgb[i][0], rgb[i][1], rgb[i][2], k * al[i]));
    C2D_Flush();
}
