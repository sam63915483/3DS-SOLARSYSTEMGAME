#include <3ds.h>
#include <citro3d.h>
#include <string.h>
#include "render.h"
#include "mesh.h"
#include "moon.h"
#include "village.h"
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

static gpu_mesh bodies[BODY_COUNT], bodies_far[BODY_COUNT], props[BODY_COUNT][8], grass[BODY_COUNT][GRASS_CELLS];
static gpu_mesh water[BODY_COUNT], ripple[BODY_COUNT], foam[BODY_COUNT], shell_in[BODY_COUNT], clouds[BODY_COUNT];
static gpu_mesh nebula;
static C3D_Tex nebula_tex; static int has_nebula_tex;
#define DUST_VERTS (DUST_N * 6)
static vertex *dust_vbo;
static gpu_mesh astro_body, astro_arm, astro_leg, flame_outer, flame_inner, stars;
static gpu_mesh rod_seg, bobber_top, bobber_bot, fish_mesh, gem;
static gpu_mesh moon_oct[BODY_COUNT][8], moon_gems[BODY_COUNT];   /* carved moons, in octants */
static gpu_mesh house_mesh[HOUSE_N], house_glow[HOUSE_N], alien_mesh[HOUSE_N], alien_glow[HOUSE_N];
#define LINE_SEGS 12
#define LINE_VERTS (LINE_SEGS * 6)
#define SPLASH_VERTS (8 * 6)
static vertex *line_vbo, *splash_vbo;   /* rebuilt every frame */
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
/* carved moons: a little more ambient than the planets so cave walls the sun cannot reach are
   never pitch black; the depth dimming and crystal light are baked into the vertex colours */
static const C3D_Material mat_moon = {
    { 0.42f, 0.42f, 0.48f }, { 0.75f, 0.75f, 0.75f }, { 0.08f, 0.08f, 0.08f }, { 0, 0, 0 }, { 0, 0, 0 },
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
        bodies_far[i] = upload(mesh_planet_lod(w, i, 3));   /* a quarter of the triangles for the far view */
        water[i] = upload(mesh_water(w, i, 0));   /* tinted by depth, still */
        ripple[i] = upload(mesh_water_ripple(w, i));   /* the drifting texture layer */
        foam[i] = upload(mesh_foam(w, i));
        for (int o = 0; o < 8; o++) props[i][o] = upload(mesh_props(w, i, o));
        mesh gm[GRASS_CELLS];
        mesh_grass_cells(w, i, gm);
        for (int g = 0; g < GRASS_CELLS; g++) grass[i][g] = upload(gm[g]);
        has_detail[i] = !b->emissive && texture_make_detail(&detail[i], texture_kind_for(b->terrain), b->seed);
        gpu_mesh none = { 0, 0, 0, 0 };
        shell_in[i] = clouds[i] = none;
        if (b->atmo) {
            /* one rim-glow shell */
            shell_in[i] = upload(mesh_sphere(b->radius + b->atmo_h * 0.7f, 24, 36, (b->atmo & 0xFFFFFF00u) | 0xE0));
            if (b->terrain != TERRAIN_EMBER)
                clouds[i] = upload(mesh_sphere_uv(b->radius + b->atmo_h * 0.62f, 28, 42, 0xFFFFFFD8u, 2.0f));
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
    nebula = upload(mesh_nebula(STAR_RADIUS * 0.97f, 20261001u));
    has_nebula_tex = texture_make_detail(&nebula_tex, DETAIL_CLOUDS, 5u);
    dust_vbo = linearAlloc(DUST_VERTS * sizeof(vertex));

    /* fishing tackle */
    rod_seg = upload(mesh_box(1, 1, 1, 0x6B4A2AFF));       /* unit box, scaled per segment */
    bobber_top = upload(mesh_sphere(0.12f, 6, 8, 0xE03030FF));
    bobber_bot = upload(mesh_sphere(0.11f, 6, 8, 0xF0F0F0FF));
    fish_mesh = upload(mesh_fish());
    gem = upload(mesh_gem(1.0f, 0xD0FF70FF));   /* unit, scaled per firefly */
    for (int k = 0; k < HOUSE_N; k++) {
        house_mesh[k] = upload(mesh_house(k)); house_glow[k] = upload(mesh_house_glow(k));
        alien_mesh[k] = upload(mesh_alien(k)); alien_glow[k] = upload(mesh_alien_glow(k));
    }
    for (int i = 0; i < BODY_COUNT; i++) {
        gpu_mesh none = { 0, 0, 0, 0 };
        moon_gems[i] = none;
        for (int o = 0; o < 8; o++) moon_oct[i][o] = none;
        if (!w->b[i].caves.enabled) continue;
        mesh octs[8];
        moon_build(w, i, octs);
        for (int o = 0; o < 8; o++) moon_oct[i][o] = upload(octs[o]);
        moon_gems[i] = upload(moon_crystals(w, i));
    }
    line_vbo = linearAlloc(LINE_VERTS * sizeof(vertex));
    splash_vbo = linearAlloc(SPLASH_VERTS * sizeof(vertex));
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

static void draw_player(const player *p, const camera *c, const fishing *f, float time) {
    if (c->inside > 0.6f) return;   /* first person, or the camera squeezed into the helmet: no model in the way */

    /* the model uses the player's smoothed body basis: upright on the ground, easing onto the
       view like a ship in flight, so takeoff and landing never snap it */
    v3 up = p->body_up;
    v3 fwd = v3_tangent(p->body_fwd, up);
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
    /* model +x is the figure's left, so the right arm hangs at -0.40 */
    if (fishing_active(f)) draw_limb(&astro_arm, &base, v3_(-0.40f, 0.40f, 0), fishing_arm_swing(f), 0);   /* right hand holds the rod, turning with it */
    else draw_limb(&astro_arm, &base, v3_(-0.40f, 0.40f, 0), arm_swing, arm_splay);
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

static void put_v(vertex *v, v3 p, v3 n, u32 col) {
    v->pos[0] = p.x; v->pos[1] = p.y; v->pos[2] = p.z;
    v->nrm[0] = n.x; v->nrm[1] = n.y; v->nrm[2] = n.z;
    v->uv[0] = v->uv[1] = 0;
    v->col[0] = (col >> 24) & 0xFF; v->col[1] = (col >> 16) & 0xFF; v->col[2] = (col >> 8) & 0xFF; v->col[3] = col & 0xFF;
}

/* Draws a CPU-filled buffer at the world origin. */
static void draw_dynamic(vertex *vbo, int count, const C3D_Material *mat) {
    if (!count) return;
    GSPGPU_FlushDataCache(vbo, count * sizeof(vertex));
    gpu_mesh g = { vbo, count, 0, 0 };
    C3D_Mtx m; Mtx_Identity(&m);
    draw(&g, &m, mat);
}

/* Rod, bobber and fish: opaque, lit. */
static void draw_fishing_solid(const fishing *f) {
    if (!fishing_active(f)) return;
    rod_frame r; fishing_rod_frame(f, &r);
    for (int i = 0; i < 4; i++) {   /* stiff butt, whippy tip: each segment bends a little more */
        v3 a = fishing_rod_point(&r, i / 4.0f), b = fishing_rod_point(&r, (i + 1) / 4.0f);
        v3 up = v3_norm(v3_sub(b, a)), fwd = v3_any_perp(up), right = v3_cross(up, fwd);
        float th = 0.05f - 0.03f * (i + 0.5f) / 4.0f;
        C3D_Mtx m; mtx_basis(&m, right, up, fwd, v3_scale(v3_add(a, b), 0.5f));
        Mtx_Scale(&m, th, v3_len(v3_sub(b, a)) * 1.03f, th);
        draw(&rod_seg, &m, &mat_lit);
    }
    if (f->bobber_visible) {
        C3D_Mtx m;
        mtx_translate(&m, v3_add(f->bobber, v3_scale(f->bobber_up, 0.03f)));
        draw(&bobber_top, &m, &mat_lit);
        mtx_translate(&m, v3_sub(f->bobber, v3_scale(f->bobber_up, 0.03f)));
        draw(&bobber_bot, &m, &mat_lit);
    }
    if (f->fish_visible) {
        v3 up = f->fish_up, fwd = f->fish_fwd, right = v3_cross(up, fwd);
        C3D_Mtx m; mtx_basis(&m, right, up, fwd, f->fish_pos);
        Mtx_Scale(&m, f->fish_scale, f->fish_scale, f->fish_scale);
        draw(&fish_mesh, &m, &mat_lit);
    }
}

/* Ambient fish under the sea near the player: opaque, drawn before the water so it tints them. */
static void draw_school(const school *s, const world *w) {
    if (s->body < 0) return;
    for (int i = 0; i < SCHOOL_N; i++) {
        if (!s->f[i].alive) continue;
        v3 pos, up, fwd;
        school_fish_basis(s, w, i, &pos, &up, &fwd);
        C3D_Mtx m; mtx_basis(&m, v3_cross(up, fwd), up, fwd, pos);
        Mtx_Scale(&m, s->f[i].scale, s->f[i].scale, s->f[i].scale);
        draw(&fish_mesh, &m, &mat_lit);
    }
}

/* Dust puffs: soft camera-facing quads; call in the blended pass. */
static void draw_dust(const dust *du, const camera *c) {
    int n = 0;
    v3 right = v3_norm(v3_cross(c->view, c->view_up)), up = c->view_up, nrm = v3_scale(c->view, -1.0f);
    for (int i = 0; i < DUST_N; i++) {
        float size, a = dust_get(du, i, &size);
        if (a < 0.02f) continue;
        const dust_puff *q = &du->p[i];
        u32 col = (q->col & 0xFFFFFF00u) | (u32)(a * 255);
        v3 r = v3_scale(right, size * 0.5f), u = v3_scale(up, size * 0.5f);
        v3 p0 = v3_sub(v3_sub(q->pos, r), u), p1 = v3_sub(v3_add(q->pos, r), u), p2 = v3_add(v3_add(q->pos, r), u), p3 = v3_add(v3_sub(q->pos, r), u);
        put_v(&dust_vbo[n++], p0, nrm, col); put_v(&dust_vbo[n++], p1, nrm, col); put_v(&dust_vbo[n++], p2, nrm, col);
        put_v(&dust_vbo[n++], p0, nrm, col); put_v(&dust_vbo[n++], p2, nrm, col); put_v(&dust_vbo[n++], p3, nrm, col);
    }
    draw_dynamic(dust_vbo, n, &mat_glow);
}

/* Fireflies: additive glowing specks; call in the blended pass. */
static void draw_fireflies(const fireflies *ff, const world *w) {
    if (ff->body < 0) return;
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ONE, GPU_ONE, GPU_ONE);
    for (int i = 0; i < FIREFLY_N; i++) {
        v3 pos;
        float k = fireflies_get(ff, w, i, &pos);
        if (k < 0.03f) continue;
        C3D_Material m = mat_glow;
        m.emission[0] = 0.5f + 0.5f * k; m.emission[1] = 0.6f + 0.4f * k; m.emission[2] = 0.3f * k;
        C3D_Mtx mm; mtx_translate(&mm, pos);
        float s = 0.08f + 0.12f * k;
        Mtx_Scale(&mm, s, s, s);
        draw(&gem, &mm, &m);
    }
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
}

static v3 bezier(v3 a, v3 c, v3 b, float t) {
    float u = 1 - t;
    return v3_add(v3_add(v3_scale(a, u * u), v3_scale(c, 2 * u * t)), v3_scale(b, t * t));
}

/* Line and splash: translucent, unlit; call in the blended pass with culling off. */
static void draw_fishing_soft(const fishing *f, const camera *c) {
    if (!fishing_active(f)) return;
    if (f->line_visible) {
        rod_frame r; fishing_rod_frame(f, &r);
        v3 a = fishing_rod_point(&r, 1.0f), b = f->bobber;
        float d = v3_len(v3_sub(b, a));
        float sag = 0.3f * (1.0f + (0.05f - 1.0f) * f->line_taut);
        v3 ctrl = v3_add(v3_scale(v3_add(a, b), 0.5f), v3_scale(f->bobber_up, -d * sag));
        const u32 col = 0xFFFFFF8Cu;   /* white, alpha 140 */
        int n = 0; v3 prev = a;
        for (int i = 0; i < LINE_SEGS; i++) {
            v3 q = bezier(a, ctrl, b, (i + 1) / (float)LINE_SEGS);
            v3 seg = v3_sub(q, prev), to_cam = v3_sub(c->pos, prev);
            v3 side = v3_cross(seg, to_cam);
            side = v3_len(side) > 1e-6f ? v3_norm(side) : v3_any_perp(v3_norm(seg));
            /* about 1.5 px wide whatever the distance */
            v3 s0 = v3_scale(side, 0.0033f * v3_len(to_cam)), s1 = v3_scale(side, 0.0033f * v3_len(v3_sub(c->pos, q)));
            v3 nrm = v3_norm(to_cam);
            v3 p0 = v3_add(prev, s0), p1 = v3_sub(prev, s0), p2 = v3_add(q, s1), p3 = v3_sub(q, s1);
            put_v(&line_vbo[n++], p0, nrm, col); put_v(&line_vbo[n++], p1, nrm, col); put_v(&line_vbo[n++], p2, nrm, col);
            put_v(&line_vbo[n++], p1, nrm, col); put_v(&line_vbo[n++], p3, nrm, col); put_v(&line_vbo[n++], p2, nrm, col);
            prev = q;
        }
        draw_dynamic(line_vbo, n, &mat_glow);
    }
    if (f->splash_t < 0.5f) {
        float k = f->splash_t / 0.5f;
        float rad = 0.2f + 1.0f * k, w = 0.06f + 0.04f * k;
        u32 col = 0xFFFFFF00u | (u32)(200 * (1.0f - k));
        v3 up = f->bobber_up, u = v3_any_perp(up), v = v3_cross(up, u);
        int n = 0;
        for (int i = 0; i < 8; i++) {
            float a0 = 6.2831853f * i / 8, a1 = 6.2831853f * (i + 1) / 8;
            v3 d0 = v3_add(v3_scale(u, cosf(a0)), v3_scale(v, sinf(a0))), d1 = v3_add(v3_scale(u, cosf(a1)), v3_scale(v, sinf(a1)));
            v3 o0 = v3_add(f->splash_pos, v3_scale(d0, rad)), o1 = v3_add(f->splash_pos, v3_scale(d1, rad));
            v3 i0 = v3_add(f->splash_pos, v3_scale(d0, rad - w)), i1 = v3_add(f->splash_pos, v3_scale(d1, rad - w));
            put_v(&splash_vbo[n++], i0, up, col); put_v(&splash_vbo[n++], o0, up, col); put_v(&splash_vbo[n++], o1, up, col);
            put_v(&splash_vbo[n++], i0, up, col); put_v(&splash_vbo[n++], o1, up, col); put_v(&splash_vbo[n++], i1, up, col);
        }
        draw_dynamic(splash_vbo, n, &mat_glow);
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

void render_scene(const world *w, const player *p, const camera *c, const fishing *f, const school *s, const fireflies *ff, const dust *du, float time) {
    u32 sky;
    float skyk = sky_color(w, p, c, &sky);
    C3D_RenderTargetClear(top, C3D_CLEAR_ALL, sky, 0);
    C3D_FrameDrawOn(top);
    prepare_state();
    cur_mode = 0;

    Mtx_PerspTilt(&proj, C3D_AngleFromDegrees(55.0f), C3D_AspectRatioTop, 0.2f, 3.0f * STAR_RADIUS, false);   /* near 0.2: a wall right at the visor never gets clipped open */
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
        /* the stars, additive, so a dimmed star can only brighten the sky behind it, never show
           as a dark speck; then the nebula over them without writing depth (it sits just inside
           the star sphere and would otherwise hide them): its wisps are the texture alpha times
           the band mask in the vertex alpha */
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ONE, GPU_ONE, GPU_ONE);
        draw(&stars, &model, &mat_stars);
        if (has_nebula_tex) {
            C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_COLOR);
            C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_SRC_ALPHA, GPU_ONE);
            draw_ex(&nebula, &model, &mat_stars, &nebula_tex, uMV);
            C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
        }
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
        C3D_CullFace(GPU_CULL_BACK_CCW);
    }

    for (int i = 0; i < BODY_COUNT; i++) {
        const body *b = &w->b[i];
        mtx_translate(&model, b->pos);
        v3 to_cam = v3_sub(c->pos, b->pos);
        float gap = v3_len(to_cam) - b->radius;
        if (gap > fmaxf(250.0f, 1.5f * b->radius)) {   /* far enough that the coarse copy looks the same */
            /* far away: the coarse copy (a carved moon's caves are not visible from out here) */
            draw_ex(&bodies_far[i], &model, b->emissive ? &mat_glow : &mat_lit, has_detail[i] ? &detail[i] : NULL, uMV);
        } else if (b->caves.enabled) {
            /* a carved moon: its surface and caves are one mesh in octants. Only when clearly
               above the ground can the far octants be skipped: from inside, or near the mouths,
               any of them may be in view down a tunnel */
            v3 d = v3_norm(to_cam);
            C3D_CullFace(GPU_CULL_NONE);   /* double sided: a mis-wound triangle shows rock behind it, never sky */
            for (int o = 0; o < 8; o++) {
                v3 od = v3_norm(v3_((o & 1) ? -1 : 1, (o & 2) ? -1 : 1, (o & 4) ? -1 : 1));
                if (gap > b->amp + 6.0f && v3_dot(od, d) < -0.5f) continue;
                draw_ex(&moon_oct[i][o], &model, &mat_moon, has_detail[i] ? &detail[i] : NULL, uMV);
            }
            C3D_CullFace(GPU_CULL_BACK_CCW);
            if (gap < 120.0f) {   /* the crystals glow, breathing a little */
                C3D_Material glow = mat_glow;
                float pulse = 0.85f + 0.15f * sinf(time * 2.2f);
                glow.emission[0] = glow.emission[1] = glow.emission[2] = pulse;
                draw(&moon_gems[i], &model, &glow);
            }
        } else {
            draw_ex(&bodies[i], &model, b->emissive ? &mat_glow : &mat_lit, has_detail[i] ? &detail[i] : NULL, uMV);
        }
        /* props are a few units tall: skip them once the planet is far enough that they
           would be smaller than a pixel anyway, and skip the octants facing away from us */
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
    draw_player(p, c, f, time);
    /* the village: houses on their pads, each alien bobbing gently and facing its visitor */
    if (w->village.n) {
        const body *vb = &w->b[w->village.body];
        if (v3_len(v3_sub(c->pos, vb->pos)) - vb->radius < 250.0f) {
            C3D_CullFace(GPU_CULL_NONE);   /* walls are single quads seen from both sides */
            for (int k = 0; k < w->village.n; k++) {
                const house *h = &w->village.h[k];
                v3 origin, right, up, fwd;
                village_frame(w, k, &origin, &right, &up, &fwd);
                C3D_Mtx m;
                mtx_basis(&m, right, up, fwd, origin);
                draw(&house_mesh[k], &m, &mat_lit);
                draw(&house_glow[k], &m, &mat_glow);
                v3 npc = v3_add(v3_add(v3_add(origin, v3_scale(right, h->npc_local.x)), v3_scale(up, h->npc_local.y)), v3_scale(fwd, h->npc_local.z));
                npc = v3_add(npc, v3_scale(up, 0.05f * sinf(time * 2.0f + k)));
                v3 nf = v3_norm(v3_tangent(h->npc_fwd, up));
                nf = v3_rotate(nf, up, 0.08f * sinf(time * 1.3f + k * 2));
                mtx_basis(&m, v3_cross(up, nf), up, nf, npc);
                draw(&alien_mesh[k], &m, &mat_lit);
                draw(&alien_glow[k], &m, &mat_glow);
            }
            C3D_CullFace(GPU_CULL_BACK_CCW);
        }
    }
    draw_fishing_solid(f);
    draw_school(s, w);

    /* seas: translucent, textured ripples drifting, both sides visible, no depth writes */
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_COLOR);
    C3D_CullFace(GPU_CULL_NONE);
    for (int i = 0; i < BODY_COUNT; i++) {
        if (!water[i].count) continue;
        mtx_translate(&model, w->b[i].pos);
        draw(&water[i], &model, &mat_water);   /* the depth-tinted body of the sea, still */
        Mtx_RotateY(&model, time * 0.012f, true);
        draw_ex(&ripple[i], &model, &mat_water, has_water_tex ? &water_tex : NULL, uMV);   /* drifting ripples */
        /* foam along the shore, breathing a little */
        C3D_Material mat_foam = mat_glow;
        float wash = 0.75f + 0.25f * sinf(time * 1.7f);
        mat_foam.emission[0] = mat_foam.emission[1] = mat_foam.emission[2] = wash;
        mtx_translate(&model, w->b[i].pos);
        draw(&foam[i], &model, &mat_foam);
    }
    draw_fishing_soft(f, c);
    draw_fireflies(ff, w);
    draw_dust(du, c);

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
    Mtx_Persp(&proj2, C3D_AngleFromDegrees(55.0f), C3D_AspectRatioTop, 0.2f, 3.0f * STAR_RADIUS, false);
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
        gpu_free(&bodies[i]); gpu_free(&bodies_far[i]); gpu_free(&water[i]); gpu_free(&shell_in[i]); gpu_free(&clouds[i]);
        for (int o = 0; o < 8; o++) gpu_free(&props[i][o]);
        for (int g = 0; g < GRASS_CELLS; g++) gpu_free(&grass[i][g]);
        if (has_detail[i]) C3D_TexDelete(&detail[i]);
    }
    if (has_water_tex) C3D_TexDelete(&water_tex);
    if (has_cloud_tex) C3D_TexDelete(&cloud_tex);
    if (has_nebula_tex) C3D_TexDelete(&nebula_tex);
    gpu_free(&nebula); linearFree(dust_vbo);
    for (int i = 0; i < BODY_COUNT; i++) { gpu_free(&ripple[i]); gpu_free(&foam[i]); }
    gpu_free(&astro_body); gpu_free(&astro_arm); gpu_free(&astro_leg);
    gpu_free(&flame_outer); gpu_free(&flame_inner); gpu_free(&stars);
    gpu_free(&rod_seg); gpu_free(&bobber_top); gpu_free(&bobber_bot); gpu_free(&fish_mesh); gpu_free(&gem);
    for (int k = 0; k < HOUSE_N; k++) { gpu_free(&house_mesh[k]); gpu_free(&house_glow[k]); gpu_free(&alien_mesh[k]); gpu_free(&alien_glow[k]); }
    for (int i = 0; i < BODY_COUNT; i++) { gpu_free(&moon_gems[i]); for (int o = 0; o < 8; o++) gpu_free(&moon_oct[i][o]); }
    linearFree(line_vbo); linearFree(splash_vbo);
    shaderProgramFree(&program);
    shaderProgramFree(&program_atmo);
    DVLB_Free(dvlb);
    DVLB_Free(dvlb_atmo);
}
