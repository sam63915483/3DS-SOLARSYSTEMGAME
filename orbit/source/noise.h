/* Deterministic value noise and hashing, portable C. */
#ifndef NOISE_H
#define NOISE_H
#include <stdint.h>
#include "vec.h"

uint32_t noise_hash(int x, int y, int z, uint32_t seed);
/* Uniform float in [0, 1) from integer coordinates. */
float noise_hashf(int x, int y, int z, uint32_t seed);
/* Smooth value noise in [0, 1]. */
float noise_value(v3 p, uint32_t seed);
/* Fractal sum of octaves, normalised to [0, 1]. */
float noise_fbm(v3 p, int octaves, uint32_t seed);

#endif
