/* Tiny 3D vector maths. No 3DS headers so the sim runs on the host too. */
#ifndef VEC_H
#define VEC_H
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct { float x, y, z; } v3;

static inline v3 v3_(float x, float y, float z) { v3 r = { x, y, z }; return r; }
static inline v3 v3_add(v3 a, v3 b) { return v3_(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline v3 v3_sub(v3 a, v3 b) { return v3_(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline v3 v3_scale(v3 a, float s) { return v3_(a.x * s, a.y * s, a.z * s); }
static inline float v3_dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline v3 v3_cross(v3 a, v3 b) {
    return v3_(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float v3_len(v3 a) { return sqrtf(v3_dot(a, a)); }
static inline v3 v3_norm(v3 a) {
    float l = v3_len(a);
    return l > 1e-8f ? v3_scale(a, 1.0f / l) : v3_(0, 1, 0);
}
static inline v3 v3_lerp(v3 a, v3 b, float t) { return v3_add(a, v3_scale(v3_sub(b, a), t)); }
/* Remove the component of a along the unit vector n. */
static inline v3 v3_tangent(v3 a, v3 n) { return v3_sub(a, v3_scale(n, v3_dot(a, n))); }
/* Any unit vector perpendicular to the unit vector n. */
static inline v3 v3_any_perp(v3 n) {
    v3 a = fabsf(n.y) < 0.9f ? v3_(0, 1, 0) : v3_(1, 0, 0);
    return v3_norm(v3_cross(a, n));
}
/* Rotate v around the unit axis by angle (Rodrigues). */
static inline v3 v3_rotate(v3 v, v3 axis, float angle) {
    float c = cosf(angle), s = sinf(angle);
    return v3_add(v3_add(v3_scale(v, c), v3_scale(v3_cross(axis, v), s)),
                  v3_scale(axis, v3_dot(axis, v) * (1 - c)));
}
/* Exponential smoothing factor for a frame of dt seconds at the given rate. */
static inline float ease(float rate, float dt) { return 1.0f - expf(-rate * dt); }

#endif
