#include <3ds.h>
#include <citro3d.h>
#include <string.h>
#include "render.h"
#include "mesh.h"
#include "texture.h"
#include "vshader_shbin.h"
#include "atmosphere_shbin.h"

#define DISPLAY_TRANSFER_FLAGS \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
    GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
    GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

#define STAR_RADIUS 4000.0f

typedef struct { void *vbo; int count; void *ibo; int icount; } gpu_mesh;

static C3D_RenderTarget *top;
static DVLB_s *dvlb, *dvlb_atmo;
static shaderProgram_s program, program_atmo;
static int uProj, uMV, aProj, aMV, aSun;
static C3D_Mtx proj, view, proj2;   /* proj2: untilted, for screen-space projection */
static C3D_LightEnv lightEnv;
static C3D_Light light;
static C3D_LightLut lutPhong;

static gpu_mesh bodies[BODY_COUNT], props[BODY_COUNT][8], grass[BODY_COUNT][GRASS_CELLS];
static gpu_mesh water[BODY_COUNT], shell_in[BODY_COUNT], clouds[BODY_COUNT];
static gpu_mesh astro_body, astro_arm, astro_leg, flame_outer, flame_inner, stars;
static C3D_Tex detail[BODY_COUNT], water_tex, cloud_tex;
static int has_detail[BODY_COUNT], has_water_tex, has_cloud_tex;
static float sun_sx, sun_sy, sun_k;

static const C3D_Material mat_lit = {
    { 0.30f, 0.30f, 0.36f },  /* ambient */
    { 0.90f, 0.90f, 0.90f },  /* diffuse */
    { 0.12f, 0.12f, 0.12f },  /* specular0 */
    { 0.0f, 0.0f, 0.0f },     /* specular1 */
    { 0.0f, 0.0f, 0.0f },     /* emission */
};
static const C3D_Material mat_water = {
    { 0.35f, 0.35f, 0.40f }, { 0.80f, 0.80f, 0.80f }, { 0.45f, 0.45f, 0.45f }, { 0, 0, 0 }, { 0, 0, 0 },
};
static const C3D_Material mat_glow = {
    { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 1.0f, 1.0f, 1.0f },
};

static gpu_mesh upload(mesh m) {
    gpu_mesh g = { 0, 0, 0, 0 };
    g.count = m.count;
    if (m.count) {
        g.vbo = linearAlloc(m.count * sizeof(vertex));
        memcpy(g.vbo, m.v, m.count * sizeof(vertex));
    }
    if (m.idx) {
        g.icount = m.idx_count;
        g.ibo = linearAlloc(m.idx_count * sizeof(uint16_t));
        memcpy(g.ibo, m.idx, m.idx_count * sizeof(uint16_t));
    }
    mesh_free(&m);
    return g;
}

static void gpu_free(gpu_mesh *g) {
    if (g->vbo) linearFree(g->vbo);
    if (g->ibo) linearFree(g->ibo);
    g->vbo = g->ibo = 0; g->count = g->icount = 0;
}

void render_init(const world *w) {
    top = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    C3D_RenderTargetSetOutput(top, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

    dvlb = DVLB_ParseFile((u32 *)vshader_shbin, vshader_shbin_size);
    shaderProgramInit(&program);
    shaderProgramSetVsh(&program, &dvlb->DVLE[0]);
    uProj = shaderInstanceGetUniformLocation(program.vertexShader, "projection");
    uMV = shaderInstanceGetUniformLocation(program.vertexShader, "modelView");

    dvlb_atmo = DVLB_ParseFile((u32 *)atmosphere_shbin, atmosphere_shbin_size);
    shaderProgramInit(&program_atmo);
    shaderProgramSetVsh(&program_atmo, &dvlb_atmo->DVLE[0]);
    aProj = shaderInstanceGetUniformLocation(program_atmo.vertexShader, "projection");
    aMV = shaderInstanceGetUniformLocation(program_atmo.vertexShader, "modelView");
    aSun = shaderInstanceGetUniformLocation(program_atmo.vertexShader, "sundir");

    C3D_LightEnvInit(&lightEnv);
    C3D_LightEnvMaterial(&lightEnv, &mat_lit);
    C3D_LightEnvAmbient(&lightEnv, 1.0f, 1.0f, 1.0f);
    LightLut_Phong(&lutPhong, 20);
    C3D_LightEnvLut(&lightEnv, GPU_LUT_D0, GPU_LUTINPUT_LN, false, &lutPhong);
    C3D_LightInit(&light, &lightEnv);
    C3D_LightColor(&light, 1.0f, 0.98f, 0.92f);

    for (int i = 0; i < BODY_COUNT; i++) {
        const body *b = &w->b[i];
        bodies[i] = upload(mesh_planet(w, i));
        water[i] = upload(mesh_water(w, i, 0x2058B4C4u));   /* deep blue, alpha 196 */
        for (int o = 0; o < 8; o++) props[i][o] = upload(mesh_props(w, i, o));
        for (int g = 0; g < GRASS_CELLS; g++) grass[i][g] = upload(mesh_grass(w, i, g));
        has_detail[i] = !b->emissive && texture_make_detail(&detail[i], texture_kind_for(b->terrain), b->seed);
        gpu_mesh none = { 0, 0, 0, 0 };
        shell_in[i] = clouds[i] = none;
        if (b->atmo) {
            /* one rim-glow shell */
            shell_in[i] = upload(mesh_sphere(b->radius + b->atmo_h * 0.7f, 24, 36, (b->atmo & 0xFFFFFF00u) | 0xE0));
            if (b->terrain != TERRAIN_EMBER)
                clouds[i] = upload(mesh_sphere_uv(b->radius + b->atmo_h * 0.45f, 28, 42, 0xFFFFFFD8u, 2.0f));
        }
    }
    has_water_tex = texture_make_detail(&water_tex, DETAIL_WATER, 77u);
    has_cloud_tex = texture_make_detail(&cloud_tex, DETAIL_CLOUDS, 91u);

    /* player: model +z is forward, +y is up, origin at the middle of the 1.8 tall body */
    astro_body = upload(mesh_astronaut(ASTRO_BODY));
    astro_arm = upload(mesh_astronaut(ASTRO_ARM));
    astro_leg = upload(mesh_astronaut(ASTRO_LEG));
    flame_outer = upload(mesh_cone(8, 0xFF7A1AFF));   /* unit cones, scaled per jet */
    flame_inner = upload(mesh_cone(8, 0xFFE878FF));
    stars = upload(mesh_stars(360, STAR_RADIUS, 14.0f, 20260929u));
}

/* Combiner setup. mode 0: vertex colour * lighting; 1: texture * vertex colour * lighting;
   2: vertex colour only (atmosphere shells). */
static void set_texenv(int mode) {
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    if (mode == 1) {
        C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
        C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
    } else {
        C3D_TexEnvSrc(env, C3D_RGB, GPU_PRIMARY_COLOR, 0, 0);
        C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
    }
    if (mode == 1) {
        /* texture alpha * vertex alpha, so cloud textures can cut holes */
        C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
        C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);
    } else {
        C3D_TexEnvSrc(env, C3D_Alpha, GPU_PRIMARY_COLOR, 0, 0);
        C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
    }
    env = C3D_GetTexEnv(1);
    C3D_TexEnvInit(env);
    env = C3D_GetTexEnv(2);
    C3D_TexEnvInit(env);
    if (mode != 2) {
        /* * fragment lighting (diffuse + ambient + emission), + specular */
        env = C3D_GetTexEnv(1);
        C3D_TexEnvSrc(env, C3D_RGB, GPU_PREVIOUS, GPU_FRAGMENT_PRIMARY_COLOR, 0);
        C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
        env = C3D_GetTexEnv(2);
        C3D_TexEnvSrc(env, C3D_RGB, GPU_PREVIOUS, GPU_FRAGMENT_SECONDARY_COLOR, 0);
        C3D_TexEnvFunc(env, C3D_RGB, GPU_ADD);
    }
}

static void prepare_state(void) {
    C3D_BindProgram(&program);

    C3D_AttrInfo *attr = C3D_GetAttrInfo();
    AttrInfo_Init(attr);
    AttrInfo_AddLoader(attr, 0, GPU_FLOAT, 3);          /* position */
    AttrInfo_AddLoader(attr, 1, GPU_FLOAT, 3);          /* normal */
    AttrInfo_AddLoader(attr, 2, GPU_UNSIGNED_BYTE, 4);  /* colour */
    AttrInfo_AddLoader(attr, 3, GPU_FLOAT, 2);          /* texcoord */

    set_texenv(0);
    for (int i = 3; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));

    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
    C3D_CullFace(GPU_CULL_BACK_CCW);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
    C3D_LightEnvBind(&lightEnv);
}

static int cur_mode = -1;

static void draw_ex(const gpu_mesh *g, const C3D_Mtx *model, const C3D_Material *mat, C3D_Tex *tex, int mv_loc) {
    if (!g->count) return;
    C3D_Mtx mv;
    Mtx_Multiply(&mv, &view, model);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, mv_loc, &mv);
    if (mat) C3D_LightEnvMaterial(&lightEnv, mat);
    int mode = tex ? 1 : (mat ? 0 : 2);
    if (mode != cur_mode) { set_texenv(mode); cur_mode = mode; }
    if (tex) C3D_TexBind(0, tex);
    C3D_BufInfo *buf = C3D_GetBufInfo();
    BufInfo_Init(buf);
    BufInfo_Add(buf, g->vbo, sizeof(vertex), 4, 0x3210);
    if (g->ibo) C3D_DrawElements(GPU_TRIANGLES, g->icount, C3D_UNSIGNED_SHORT, g->ibo);
    else C3D_DrawArrays(GPU_TRIANGLES, 0, g->count);
}

static void draw(const gpu_mesh *g, const C3D_Mtx *model, const C3D_Material *mat) { draw_ex(g, model, mat, NULL, uMV); }

static void mtx_translate(C3D_Mtx *m, v3 p) {
    Mtx_Identity(m);
    Mtx_Translate(m, p.x, p.y, p.z, true);
}

/* Model matrix with the given basis (columns) and origin. */
static void mtx_basis(C3D_Mtx *m, v3 right, v3 up, v3 fwd, v3 p) {
    m->r[0] = FVec4_New(right.x, up.x, fwd.x, p.x);
    m->r[1] = FVec4_New(right.y, up.y, fwd.y, p.y);
    m->r[2] = FVec4_New(right.z, up.z, fwd.z, p.z);
    m->r[3] = FVec4_New(0, 0, 0, 1);
}

/* A flame in model space: a two-tone cone from origin_m along dir_m, flickering. */
static void draw_jet(const C3D_Mtx *base, v3 origin_m, v3 dir_m, float len, float time, float phase) {
    float flick = 0.8f + 0.2f * sinf(time * 37.0f + phase) + 0.08f * sinf(time * 91.0f + phase * 2.0f);
    len *= flick;
    v3 up = v3_norm(dir_m);
    v3 fwd = v3_any_perp(up), right = v3_cross(up, fwd);
    C3D_Mtx local, m;
    mtx_basis(&local, right, up, fwd, origin_m);
    Mtx_Scale(&local, 0.13f * flick, len, 0.13f * flick);
    Mtx_Multiply(&m, base, &local);
    draw(&flame_outer, &m, &mat_glow);
    mtx_basis(&local, right, up, fwd, v3_add(origin_m, v3_scale(up, 0.02f)));
    Mtx_Scale(&local, 0.07f, len * 0.6f, 0.07f);
    Mtx_Multiply(&m, base, &local);
    draw(&flame_inner, &m, &mat_glow);
}

/* One limb: pivot offset in model space, swung around the model's x axis. */
static void draw_limb(const gpu_mesh *g, const C3D_Mtx *base, v3 pivot, float swing, float splay) {
    C3D_Mtx off, m;
    mtx_translate(&off, pivot);
    Mtx_RotateX(&off, swing, true);
    if (splay != 0) Mtx_RotateZ(&off, splay, true);
    Mtx_Multiply(&m, base, &off);
    draw(g, &m, &mat_lit);
}

static void draw_player(const player *p, const camera *c, float time) {
    if (c->fp > 0.6f) return;   /* first person: no model in the way */

    /* the model uses the camera's smoothed basis so both roll together when "up" changes;
       in flight it points along the view like a ship */
    v3 up = player_on_foot(p) ? c->up : c->view_up;
    v3 fwd = player_on_foot(p) ? v3_tangent(p->facing, up) : c->view;
    fwd = v3_len(fwd) < 1e-3f ? v3_any_perp(up) : v3_norm(fwd);
    v3 right = v3_cross(up, fwd);
    C3D_Mtx base;
    mtx_basis(&base, right, up, fwd, p->pos);

    draw(&astro_body, &base, &mat_lit);

    /* limbs: walk cycle on the ground, swept back in flight */
    float swing, arm_swing, arm_splay, leg_back;
    if (player_on_foot(p)) {
        float k = p->speed / 8.0f; if (k > 1) k = 1;
        swing = sinf(p->anim) * 0.7f * k;
        arm_swing = -swing * 0.7f; arm_splay = 0.12f; leg_back = 0;
    } else {
        swing = 0; arm_swing = -0.55f; arm_splay = 0.35f; leg_back = -0.3f;
    }
    draw_limb(&astro_leg, &base, v3_(-0.17f, -0.15f, 0), swing + leg_back, 0);
    draw_limb(&astro_leg, &base, v3_(0.17f, -0.15f, 0), -swing + leg_back, 0);
    draw_limb(&astro_arm, &base, v3_(-0.40f, 0.40f, 0), arm_swing, arm_splay);
    draw_limb(&astro_arm, &base, v3_(0.40f, 0.40f, 0), -arm_swing * (player_on_foot(p) ? 1 : -1), -arm_splay);

    /* jets: exhaust goes opposite to the thrust. pack plate spans y -0.2..0.44, z -0.39..-0.19;
       the nozzle is at the bottom centre, the tanks top out at y 0.5 */
    if (p->jet_up)   draw_jet(&base, v3_(0, -0.30f, -0.34f), v3_(0, -1, 0), 1.0f, time, 0);
    if (p->jet_down) draw_jet(&base, v3_(0, 0.50f, -0.34f), v3_(0, 1, 0), 0.8f, time, 1);
    if (p->jet_fwd)  draw_jet(&base, v3_(0, 0.12f, -0.40f), v3_(0, 0, -1), 1.1f, time, 2);
    if (v3_len(p->brake_dir) > 0.5f) {
        /* brake: exhaust along the direction of travel, expressed in the model frame */
        v3 dm = v3_(v3_dot(p->brake_dir, right), v3_dot(p->brake_dir, up), v3_dot(p->brake_dir, fwd));
        draw_jet(&base, v3_(0, 0.12f, -0.34f), dm, 1.0f, time, 5);
    }
    if (p->jet_roll > 0.05f) {   /* roll right: left shoulder fires up-left, right hip fires down-right */
        draw_jet(&base, v3_(-0.28f, 0.44f, -0.34f), v3_(-1, 1, 0), 0.55f, time, 3);
        draw_jet(&base, v3_(0.28f, -0.20f, -0.34f), v3_(1, -1, 0), 0.55f, time, 4);
    } else if (p->jet_roll < -0.05f) {
        draw_jet(&base, v3_(0.28f, 0.44f, -0.34f), v3_(1, 1, 0), 0.55f, time, 3);
        draw_jet(&base, v3_(-0.28f, -0.20f, -0.34f), v3_(-1, -1, 0), 0.55f, time, 4);
    }
}

static float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

/* Sky colour seen from the camera: the dominant body's atmosphere, fading with altitude and
   with the sun below the horizon, warm at the terminator. Returns the strength 0..1. */
static float sky_color(const world *w, const player *p, const camera *c, u32 *out) {
    const body *b = &w->b[p->dom];
    float r = 2, g = 2, bl = 10, k = 0;
    if (b->atmo) {
        v3 rel = v3_sub(c->pos, b->pos);
        float alt = v3_len(rel) - b->radius;
        float dens = clamp01(1.0f - alt / b->atmo_h);
        v3 up = v3_norm(rel), sun = v3_norm(v3_sub(w->b[BODY_SUN].pos, c->pos));
        float e = v3_dot(up, sun);
        float day = clamp01((e + 0.12f) / 0.40f);
        float strength = (b->atmo & 0xFF) / 255.0f;
        k = dens * day * strength;
        float ar = (b->atmo >> 24) & 0xFF, ag = (b->atmo >> 16) & 0xFF, ab = (b->atmo >> 8) & 0xFF;
        /* warm tint when the sun is near the horizon */
        float warm = clamp01(1.0f - fabsf(e) / 0.25f) * dens * 0.7f;
        ar = ar + (235 - ar) * warm; ag = ag + (130 - ag) * warm; ab = ab + (60 - ab) * warm;
        r = 2 + (ar - 2) * k; g = 2 + (ag - 2) * k; bl = 10 + (ab - 10) * k;
    }
    *out = ((u32)r << 24) | ((u32)g << 16) | ((u32)bl << 8) | 0xFF;
    return k;
}

void render_scene(const world *w, const player *p, const camera *c, float time) {
    u32 sky;
    float skyk = sky_color(w, p, c, &sky);
    C3D_RenderTargetClear(top, C3D_CLEAR_ALL, sky, 0);
    C3D_FrameDrawOn(top);
    prepare_state();
    cur_mode = 0;

    Mtx_PerspTilt(&proj, C3D_AngleFromDegrees(55.0f), C3D_AspectRatioTop, 0.5f, 3.0f * STAR_RADIUS, false);
    Mtx_LookAt(&view, FVec3_New(c->pos.x, c->pos.y, c->pos.z),
               FVec3_New(c->target.x, c->target.y, c->target.z),
               FVec3_New(c->view_up.x, c->view_up.y, c->view_up.z), false);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uProj, &proj);

    /* the sun is the light, positioned in view space */
    v3 sp = w->b[BODY_SUN].pos;
    C3D_FVec lp = Mtx_MultiplyFVec4(&view, FVec4_New(sp.x, sp.y, sp.z, 1.0f));
    C3D_LightPosition(&light, &lp);

    C3D_Mtx model;
    /* stars follow the camera so they never get closer; they fade as the sky brightens */
    float star_k = 1.0f - skyk * 1.8f;
    if (star_k > 0.02f) {
        C3D_Material mat_stars = mat_glow;
        mat_stars.emission[0] = mat_stars.emission[1] = mat_stars.emission[2] = star_k > 1 ? 1 : star_k;
        mtx_translate(&model, c->pos);
        C3D_CullFace(GPU_CULL_NONE);
        /* additive: a dimmed star can only brighten the sky behind it, never show as a dark speck */
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ONE, GPU_ONE, GPU_ONE);
        draw(&stars, &model, &mat_stars);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
        C3D_CullFace(GPU_CULL_BACK_CCW);
    }

    for (int i = 0; i < BODY_COUNT; i++) {
        const body *b = &w->b[i];
        mtx_translate(&model, b->pos);
        draw_ex(&bodies[i], &model, b->emissive ? &mat_glow : &mat_lit, has_detail[i] ? &detail[i] : NULL, uMV);
        /* props are a few units tall: skip them once the planet is far enough that they
           would be smaller than a pixel anyway, and skip the octants facing away from us */
        v3 to_cam = v3_sub(c->pos, b->pos);
        float gap = v3_len(to_cam) - b->radius;
        if (gap < 700.0f) {
            v3 d = v3_norm(to_cam);
            for (int o = 0; o < 8; o++) {
                if (!props[i][o].count) continue;
                v3 od = v3_norm(v3_((o & 1) ? -1 : 1, (o & 2) ? -1 : 1, (o & 4) ? -1 : 1));
                if (v3_dot(od, d) < -0.25f) continue;   /* over the horizon */
                draw(&props[i][o], &model, &mat_lit);
            }
            /* grass only near the camera: cells whose centre is within about 50 degrees */
            if (gap < 120.0f) {
                C3D_CullFace(GPU_CULL_NONE);
                for (int g = 0; g < GRASS_CELLS; g++) {
                    if (!grass[i][g].count) continue;
                    if (v3_dot(mesh_grass_cell_dir(g), d) < 0.64f) continue;
                    draw(&grass[i][g], &model, &mat_lit);
                }
                C3D_CullFace(GPU_CULL_BACK_CCW);
            }
        }
    }
    draw_player(p, c, time);

    /* seas: translucent, textured ripples drifting, both sides visible, no depth writes */
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_COLOR);
    C3D_CullFace(GPU_CULL_NONE);
    for (int i = 0; i < BODY_COUNT; i++) {
        if (!water[i].count) continue;
        mtx_translate(&model, w->b[i].pos);
        Mtx_RotateY(&model, time * 0.012f, true);
        draw_ex(&water[i], &model, &mat_water, has_water_tex ? &water_tex : NULL, uMV);
    }

    /* clouds: lit, textured alpha, drifting */
    for (int i = 0; i < BODY_COUNT; i++) {
        if (!clouds[i].count || !has_cloud_tex) continue;
        mtx_translate(&model, w->b[i].pos);
        Mtx_RotateY(&model, time * 0.006f, true);
        draw_ex(&clouds[i], &model, &mat_lit, &cloud_tex, uMV);
    }

    /* atmosphere shells: rim glow with their own shader, no lighting. Each layer fades out
       as the camera passes through it so it never reads as a hard bubble. */
    C3D_BindProgram(&program_atmo);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, aProj, &proj);
    C3D_LightEnvBind(NULL);
    for (int i = 0; i < BODY_COUNT; i++) {
        const body *b = &w->b[i];
        if (!shell_in[i].count) continue;
        v3 sd = v3_norm(v3_sub(w->b[BODY_SUN].pos, b->pos));
        C3D_FVec sv = Mtx_MultiplyFVec4(&view, FVec4_New(sd.x, sd.y, sd.z, 0.0f));
        C3D_FVUnifSet(GPU_VERTEX_SHADER, aSun, sv.x, sv.y, sv.z, 0.0f);
        mtx_translate(&model, b->pos);
        draw_ex(&shell_in[i], &model, NULL, NULL, aMV);
    }
    C3D_LightEnvBind(&lightEnv);
    C3D_BindProgram(&program);
    C3D_CullFace(GPU_CULL_BACK_CCW);
    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);

    /* where the sun lands on the screen, for the lens flare (an untilted projection gives
       ordinary screen axes) */
    sun_k = 0;
    Mtx_Persp(&proj2, C3D_AngleFromDegrees(55.0f), C3D_AspectRatioTop, 0.5f, 3.0f * STAR_RADIUS, false);
    {
        C3D_FVec vs = Mtx_MultiplyFVec4(&view, FVec4_New(sp.x, sp.y, sp.z, 1.0f));
        C3D_FVec cs = Mtx_MultiplyFVec4(&proj2, vs);
        if (cs.w > 1.0f) {
            float nx = cs.x / cs.w, ny = cs.y / cs.w;
            sun_sx = (nx * 0.5f + 0.5f) * 400.0f;
            sun_sy = (1.0f - (ny * 0.5f + 0.5f)) * 240.0f;
            float d = sqrtf(nx * nx + ny * ny);
            sun_k = clamp01(1.35f - d);
            /* hidden behind a planet? */
            v3 ray = v3_sub(sp, c->pos);
            float len = v3_len(ray);
            v3 rd = v3_scale(ray, 1.0f / len);
            for (int i = 1; i < BODY_COUNT && sun_k > 0; i++) {
                v3 oc = v3_sub(w->b[i].pos, c->pos);
                float t = v3_dot(oc, rd);
                if (t < 0 || t > len) continue;
                v3 closest = v3_sub(oc, v3_scale(rd, t));
                float rr = w->b[i].radius + w->b[i].amp;
                if (v3_dot(closest, closest) < rr * rr) sun_k = 0;
            }
            sun_k *= 1.0f - 0.5f * skyk;   /* a little softer in a bright sky */
        }
    }
}

float render_sun_screen(float *sx, float *sy) { *sx = sun_sx; *sy = sun_sy; return sun_k; }

int render_project(v3 p, float *sx, float *sy) {
    C3D_FVec vs = Mtx_MultiplyFVec4(&view, FVec4_New(p.x, p.y, p.z, 1.0f));
    C3D_FVec cs = Mtx_MultiplyFVec4(&proj2, vs);
    if (cs.w <= 0.5f) return 0;
    *sx = (cs.x / cs.w * 0.5f + 0.5f) * 400.0f;
    *sy = (1.0f - (cs.y / cs.w * 0.5f + 0.5f)) * 240.0f;
    return 1;
}
C3D_RenderTarget *render_top_target(void) { return top; }

void render_fini(void) {
    for (int i = 0; i < BODY_COUNT; i++) {
        gpu_free(&bodies[i]); gpu_free(&water[i]); gpu_free(&shell_in[i]); gpu_free(&clouds[i]);
        for (int o = 0; o < 8; o++) gpu_free(&props[i][o]);
        for (int g = 0; g < GRASS_CELLS; g++) gpu_free(&grass[i][g]);
        if (has_detail[i]) C3D_TexDelete(&detail[i]);
    }
    if (has_water_tex) C3D_TexDelete(&water_tex);
    if (has_cloud_tex) C3D_TexDelete(&cloud_tex);
    gpu_free(&astro_body); gpu_free(&astro_arm); gpu_free(&astro_leg);
    gpu_free(&flame_outer); gpu_free(&flame_inner); gpu_free(&stars);
    shaderProgramFree(&program);
    shaderProgramFree(&program_atmo);
    DVLB_Free(dvlb);
    DVLB_Free(dvlb_atmo);
}
