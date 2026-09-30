#include "texture.h"
#include "noise.h"
#include <string.h>
#include <stdlib.h>

#define TEX_SIZE 128   /* sharp up close; the mipmaps take over as you fly away */

/* Periodic 2D value noise over `cells` lattice cells across [0,1), so it tiles. */
static float pnoise(float u, float v, int cells, uint32_t seed) {
    float x = u * cells, y = v * cells;
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float tx = x - ix, ty = y - iy;
    tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
    int x0 = ((ix % cells) + cells) % cells, x1 = (x0 + 1) % cells;
    int y0 = ((iy % cells) + cells) % cells, y1 = (y0 + 1) % cells;
    float a = noise_hashf(x0, y0, 0, seed), b = noise_hashf(x1, y0, 0, seed);
    float c = noise_hashf(x0, y1, 0, seed), d = noise_hashf(x1, y1, 0, seed);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

static float pfbm(float u, float v, int cells, int octaves, uint32_t seed) {
    float sum = 0, amp = 1, total = 0;
    for (int o = 0; o < octaves; o++) {
        sum += amp * pnoise(u, v, cells, seed + 31u * (uint32_t)o);
        total += amp; amp *= 0.5f; cells *= 2;
    }
    return sum / total;
}

static float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

/* Level-0 texel colour as floats 0..1 (r, g, b, a) for the given kind. */
static void texel(detail_kind kind, float u, float v, int px, int py, uint32_t seed, float *r, float *g, float *b, float *a) {
    float br = 1.0f, tr = 1.0f, tg = 1.0f, tb = 1.0f;
    *a = 1.0f;
    switch (kind) {
    case DETAIL_CLOUDS: {
        /* soft cloud shapes in the alpha, white colour */
        float n = pfbm(u, v, 3, 4, seed);
        float t = (n - 0.50f) / 0.16f;
        *a = clamp01(t) * clamp01(t) * (3 - 2 * clamp01(t));
        br = 1.0f;
        break;
    }
    case DETAIL_GRASS: {
        /* patches of lighter and darker turf, streaky blades, the odd bare speck of earth */
        float base = pfbm(u, v, 4, 3, seed);
        float blades = pfbm(u * 1.0f, v * 6.0f, 18, 2, seed + 7u);   /* stretched along v: blades */
        float fine = pfbm(u, v, 24, 2, seed + 11u);
        br = 0.78f + (base - 0.5f) * 0.6f;
        if (blades > 0.60f) br -= 0.18f;
        else if (blades < 0.40f) br += 0.12f;
        if (fine > 0.72f) { br *= 0.8f; tr = 1.08f; tg = 0.96f; tb = 0.85f; }   /* earth showing through */
        else { tr = 0.92f; tg = 1.06f; tb = 0.88f; }
        break;
    }
    case DETAIL_ROCK: {
        /* two scales of cracks, pitted grain, and pale mineral flecks */
        float ridge = fabsf(pfbm(u, v, 5, 2, seed) - 0.5f) * 2.0f;          /* 0 at the big cracks */
        float ridge2 = fabsf(pfbm(u, v, 13, 2, seed + 5u) - 0.5f) * 2.0f;    /* 0 at the fine cracks */
        float grain = pfbm(u, v, 20, 3, seed + 3u);
        float crack = clamp01(ridge * 4.0f), crack2 = clamp01(ridge2 * 6.0f);
        br = 0.50f + crack * 0.30f + crack2 * 0.14f + (grain - 0.5f) * 0.28f;
        if (noise_hashf(px, py, 3, seed) > 0.975f) br += 0.25f;   /* fleck */
        tr = 1.04f; tg = 0.99f; tb = 0.94f;
        break;
    }
    case DETAIL_SNOW: {
        /* wind-packed drifts with faint blue cracks and sparkle */
        float base = pfbm(u * 3.0f, v, 5, 3, seed);   /* stretched: drifts */
        float crack = fabsf(pfbm(u, v, 7, 2, seed + 9u) - 0.5f) * 2.0f;
        br = 0.88f + (base - 0.5f) * 0.26f;
        if (crack < 0.08f) { br -= 0.18f; tb = 1.12f; }
        else tb = 1.04f;
        if (noise_hashf(px, py, 3, seed) > 0.985f) br = 1.18f;   /* sparkle */
        tr = 0.97f; tg = 0.99f;
        break;
    }
    case DETAIL_WATER: {
        float n = pfbm(u, v, 6, 2, seed);
        float line = 1.0f - fabsf(n - 0.5f) * 2.0f;               /* bright ridges */
        br = 0.84f + clamp01(line - 0.55f) * 0.55f;
        tr = 0.95f; tg = 1.0f; tb = 1.05f;
        break;
    }
    }
    *r = clamp01(br * tr); *g = clamp01(br * tg); *b = clamp01(br * tb);
}

/* Index of pixel (x, y) in the GPU's tiled (8x8 Morton) layout of a w-wide texture. */
static int tiled_index(int x, int y, int w) {
    int tile = (y / 8) * (w / 8) + (x / 8);
    int lx = x & 7, ly = y & 7;
    int within = (lx & 1) | ((ly & 1) << 1) | ((lx & 2) << 1) | ((ly & 2) << 2) | ((lx & 4) << 2) | ((ly & 4) << 3);
    return tile * 64 + within;
}

/* The GPU shows the naive layout rotated a quarter turn (see font.c), so store rotated. */
static void put_pixel(u32 *px, int w, int x, int y, u32 value) {
    px[tiled_index(y, w - 1 - x, w)] = value;
}

detail_kind texture_kind_for(terrain_kind t) {
    switch (t) {
    case TERRAIN_EMBER: return DETAIL_ROCK;
    case TERRAIN_FROST: return DETAIL_SNOW;
    case TERRAIN_MOON: return DETAIL_ROCK;
    default: return DETAIL_GRASS;
    }
}

bool texture_make_detail(C3D_Tex *tex, detail_kind kind, uint32_t seed) {
    if (!C3D_TexInitMipmap(tex, TEX_SIZE, TEX_SIZE, GPU_RGBA8)) return false;
    /* level 0 as floats, four channels */
    float *img = malloc(sizeof(float) * TEX_SIZE * TEX_SIZE * 4);
    for (int y = 0; y < TEX_SIZE; y++)
        for (int x = 0; x < TEX_SIZE; x++) {
            float *c = &img[(y * TEX_SIZE + x) * 4];
            texel(kind, (x + 0.5f) / TEX_SIZE, (y + 0.5f) / TEX_SIZE, x, y, seed, &c[0], &c[1], &c[2], &c[3]);
        }
    int w = TEX_SIZE;
    for (int level = 0; level <= tex->maxLevel; level++) {
        u32 *px = C3D_TexGetImagePtr(tex, tex->data, level, NULL);
        for (int y = 0; y < w; y++) {
            for (int x = 0; x < w; x++) {
                const float *c = &img[(y * w + x) * 4];
                u32 r = (u32)(c[0] * 255.0f + 0.5f), g = (u32)(c[1] * 255.0f + 0.5f);
                u32 b = (u32)(c[2] * 255.0f + 0.5f), a = (u32)(c[3] * 255.0f + 0.5f);
                put_pixel(px, w, x, y, (r << 24) | (g << 16) | (b << 8) | a);
            }
        }
        /* box-filter down for the next level */
        int hw = w / 2;
        if (hw < 8) break;
        for (int y = 0; y < hw; y++) {
            for (int x = 0; x < hw; x++) {
                for (int k = 0; k < 4; k++) {
                    float s = img[((2 * y) * w + 2 * x) * 4 + k] + img[((2 * y) * w + 2 * x + 1) * 4 + k]
                            + img[((2 * y + 1) * w + 2 * x) * 4 + k] + img[((2 * y + 1) * w + 2 * x + 1) * 4 + k];
                    img[(y * hw + x) * 4 + k] = s * 0.25f;
                }
            }
        }
        w = hw;
    }
    free(img);
    C3D_TexSetFilter(tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetFilterMipmap(tex, GPU_LINEAR);
    C3D_TexSetWrap(tex, GPU_REPEAT, GPU_REPEAT);
    GSPGPU_FlushDataCache(tex->data, C3D_TexCalcTotalSize(tex->size, tex->maxLevel));
    return true;
}
