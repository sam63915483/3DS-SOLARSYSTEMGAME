#include "noise.h"

uint32_t noise_hash(int x, int y, int z, uint32_t seed) {
    uint32_t h = seed * 0x9E3779B1u;
    h ^= (uint32_t)x * 0x85EBCA77u; h = (h ^ (h >> 15)) * 0xC2B2AE3Du;
    h ^= (uint32_t)y * 0x27D4EB2Fu; h = (h ^ (h >> 13)) * 0x165667B1u;
    h ^= (uint32_t)z * 0x9E3779B1u; h ^= h >> 16;
    h *= 0x85EBCA77u; h ^= h >> 13;
    return h;
}

float noise_hashf(int x, int y, int z, uint32_t seed) {
    return (noise_hash(x, y, z, seed) >> 8) / 16777216.0f;
}

static float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

float noise_value(v3 p, uint32_t seed) {
    float fx = floorf(p.x), fy = floorf(p.y), fz = floorf(p.z);
    int x = (int)fx, y = (int)fy, z = (int)fz;
    float tx = smooth(p.x - fx), ty = smooth(p.y - fy), tz = smooth(p.z - fz);
    float c000 = noise_hashf(x, y, z, seed),         c100 = noise_hashf(x + 1, y, z, seed);
    float c010 = noise_hashf(x, y + 1, z, seed),     c110 = noise_hashf(x + 1, y + 1, z, seed);
    float c001 = noise_hashf(x, y, z + 1, seed),     c101 = noise_hashf(x + 1, y, z + 1, seed);
    float c011 = noise_hashf(x, y + 1, z + 1, seed), c111 = noise_hashf(x + 1, y + 1, z + 1, seed);
    float x00 = c000 + (c100 - c000) * tx, x10 = c010 + (c110 - c010) * tx;
    float x01 = c001 + (c101 - c001) * tx, x11 = c011 + (c111 - c011) * tx;
    float y0 = x00 + (x10 - x00) * ty, y1 = x01 + (x11 - x01) * ty;
    return y0 + (y1 - y0) * tz;
}

float noise_fbm(v3 p, int octaves, uint32_t seed) {
    float sum = 0, amp = 1, total = 0;
    for (int o = 0; o < octaves; o++) {
        sum += amp * noise_value(p, seed + 17u * (uint32_t)o);
        total += amp;
        p = v3_scale(p, 2.03f);
        amp *= 0.5f;
    }
    return sum / total;
}
