/* Procedural detail textures for the GPU, generated at start-up with mipmaps. */
#ifndef TEXTURE_H
#define TEXTURE_H
#include <citro3d.h>
#include "world.h"

typedef enum { DETAIL_GRASS, DETAIL_ROCK, DETAIL_SNOW, DETAIL_WATER, DETAIL_CLOUDS } detail_kind;

/* Builds a 64x64 tiling RGBA8 texture with mipmaps into tex. Returns false on failure. */
bool texture_make_detail(C3D_Tex *tex, detail_kind kind, uint32_t seed);
/* Which detail texture a body's terrain uses. */
detail_kind texture_kind_for(terrain_kind t);

#endif
