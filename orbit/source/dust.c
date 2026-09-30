#include "dust.h"
#include "fight.h"   /* frandf */
#include <string.h>

void dust_init(dust *d) { memset(d, 0, sizeof *d); d->rng = 0xD05Du; }

/* Dust colour of a body's ground. */
static uint32_t ground_dust(const body *b) {
    switch (b->terrain) {
    case TERRAIN_EMBER: return 0x8A5040FFu;
    case TERRAIN_FROST: return 0xE8F0FFFFu;
    case TERRAIN_MOON: return 0xA8A8B0FFu;
    default: return 0xC0AC88FFu;
    }
}

void dust_burst(dust *d, const world *w, int bi, v3 pos, v3 up, int n, float speed, float size) {
    v3 t1 = v3_any_perp(up), t2 = v3_cross(up, t1);
    for (int k = 0; k < n; k++) {
        int slot = -1; float oldest = -1;
        for (int i = 0; i < DUST_N; i++) {   /* a free slot, else the puff nearest its end */
            if (!d->p[i].alive) { slot = i; break; }
            float age = d->p[i].max_life - d->p[i].life;
            if (age > oldest) { oldest = age; slot = i; }
        }
        dust_puff *q = &d->p[slot];
        float a = frandf(&d->rng, 0, 6.2831853f), s = speed * frandf(&d->rng, 0.5f, 1.0f);
        q->alive = 1; q->body = bi;
        q->pos = v3_add(pos, v3_scale(up, 0.1f));
        q->vel = v3_add(v3_add(v3_scale(t1, cosf(a) * s), v3_scale(t2, sinf(a) * s)), v3_scale(up, s * 0.35f));
        q->max_life = q->life = frandf(&d->rng, 0.5f, 0.9f);
        q->size = size * frandf(&d->rng, 0.7f, 1.3f);
        q->col = ground_dust(&w->b[bi]);
    }
}

void dust_update(dust *d, const world *w, float dt) {
    for (int i = 0; i < DUST_N; i++) {
        dust_puff *q = &d->p[i];
        if (!q->alive) continue;
        const body *b = &w->b[q->body];
        q->pos = v3_add(q->pos, v3_sub(b->pos, b->prev));   /* ride along */
        q->pos = v3_add(q->pos, v3_scale(q->vel, dt));
        q->vel = v3_scale(q->vel, 1.0f - 2.5f * dt);          /* the air slows it */
        q->size += 1.6f * dt;                                  /* and it spreads */
        q->life -= dt;
        if (q->life <= 0) q->alive = 0;
    }
}

float dust_get(const dust *d, int i, float *size) {
    const dust_puff *q = &d->p[i];
    if (!q->alive) return 0;
    *size = q->size;
    float k = q->life / q->max_life;
    return k * 0.55f;
}
