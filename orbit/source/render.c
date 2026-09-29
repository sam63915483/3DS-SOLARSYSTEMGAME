#include <3ds.h>
#include <citro3d.h>
#include <string.h>
#include "render.h"
#include "mesh.h"
#include "vshader_shbin.h"

#define DISPLAY_TRANSFER_FLAGS \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
    GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
    GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

#define STAR_RADIUS 4000.0f

typedef struct { void *vbo; int count; void *ibo; int icount; } gpu_mesh;

static C3D_RenderTarget *top;
static DVLB_s *dvlb;
static shaderProgram_s program;
static int uProj, uMV;
static C3D_Mtx proj, view;
static C3D_LightEnv lightEnv;
static C3D_Light light;
static C3D_LightLut lutPhong;

static gpu_mesh bodies[BODY_COUNT], props[BODY_COUNT], astro_body, astro_arm, astro_leg, flame_outer, flame_inner, stars;

static const C3D_Material mat_lit = {
    { 0.30f, 0.30f, 0.36f },  /* ambient */
    { 0.90f, 0.90f, 0.90f },  /* diffuse */
    { 0.12f, 0.12f, 0.12f },  /* specular0 */
    { 0.0f, 0.0f, 0.0f },     /* specular1 */
    { 0.0f, 0.0f, 0.0f },     /* emission */
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

    C3D_LightEnvInit(&lightEnv);
    C3D_LightEnvMaterial(&lightEnv, &mat_lit);
    C3D_LightEnvAmbient(&lightEnv, 1.0f, 1.0f, 1.0f);
    LightLut_Phong(&lutPhong, 20);
    C3D_LightEnvLut(&lightEnv, GPU_LUT_D0, GPU_LUTINPUT_LN, false, &lutPhong);
    C3D_LightInit(&light, &lightEnv);
    C3D_LightColor(&light, 1.0f, 0.98f, 0.92f);

    /* mesh density scales with size so the hills stay smooth; indexed, so cheap to draw */
    static const int rings_for[BODY_COUNT] = { 32, 36, 56, 44 };
    static const int segs_for[BODY_COUNT] = { 48, 54, 84, 66 };
    for (int i = 0; i < BODY_COUNT; i++) {
        bodies[i] = upload(mesh_planet(w, i, rings_for[i], segs_for[i]));
        props[i] = upload(mesh_props(w, i));
    }
    /* player: model +z is forward, +y is up, origin at the middle of the 1.8 tall body */
    astro_body = upload(mesh_astronaut(ASTRO_BODY));
    astro_arm = upload(mesh_astronaut(ASTRO_ARM));
    astro_leg = upload(mesh_astronaut(ASTRO_LEG));
    flame_outer = upload(mesh_cone(8, 0xFF7A1AFF));   /* unit cones, scaled per jet */
    flame_inner = upload(mesh_cone(8, 0xFFE878FF));
    stars = upload(mesh_stars(360, STAR_RADIUS, 14.0f, 20260929u));
}

static void prepare_state(void) {
    C3D_BindProgram(&program);

    C3D_AttrInfo *attr = C3D_GetAttrInfo();
    AttrInfo_Init(attr);
    AttrInfo_AddLoader(attr, 0, GPU_FLOAT, 3);          /* position */
    AttrInfo_AddLoader(attr, 1, GPU_FLOAT, 3);          /* normal */
    AttrInfo_AddLoader(attr, 2, GPU_UNSIGNED_BYTE, 4);  /* colour */

    /* stage 0: vertex colour * fragment lighting (diffuse + ambient + emission) */
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_PRIMARY_COLOR, GPU_FRAGMENT_PRIMARY_COLOR, 0);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
    C3D_TexEnvSrc(env, C3D_Alpha, GPU_PRIMARY_COLOR, 0, 0);
    C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
    /* stage 1: + specular */
    env = C3D_GetTexEnv(1);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_PREVIOUS, GPU_FRAGMENT_SECONDARY_COLOR, 0);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_ADD);
    for (int i = 2; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));

    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
    C3D_CullFace(GPU_CULL_BACK_CCW);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
    C3D_LightEnvBind(&lightEnv);
}

static void draw(const gpu_mesh *g, const C3D_Mtx *model, const C3D_Material *mat) {
    C3D_Mtx mv;
    Mtx_Multiply(&mv, &view, model);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uMV, &mv);
    C3D_LightEnvMaterial(&lightEnv, mat);
    C3D_BufInfo *buf = C3D_GetBufInfo();
    BufInfo_Init(buf);
    BufInfo_Add(buf, g->vbo, sizeof(vertex), 3, 0x210);
    if (g->ibo) C3D_DrawElements(GPU_TRIANGLES, g->icount, C3D_UNSIGNED_SHORT, g->ibo);
    else C3D_DrawArrays(GPU_TRIANGLES, 0, g->count);
}

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
    v3 up = p->grounded ? c->up : c->view_up;
    v3 fwd = p->grounded ? v3_tangent(p->facing, up) : c->view;
    fwd = v3_len(fwd) < 1e-3f ? v3_any_perp(up) : v3_norm(fwd);
    v3 right = v3_cross(up, fwd);
    C3D_Mtx base, m, off;
    mtx_basis(&base, right, up, fwd, p->pos);

    (void)m; (void)off;
    draw(&astro_body, &base, &mat_lit);

    /* limbs: walk cycle on the ground, swept back in flight */
    float swing, arm_swing, arm_splay, leg_back;
    if (p->grounded) {
        float k = p->speed / 8.0f; if (k > 1) k = 1;
        swing = sinf(p->anim) * 0.7f * k;
        arm_swing = -swing * 0.7f; arm_splay = 0.12f; leg_back = 0;
    } else {
        swing = 0; arm_swing = -0.55f; arm_splay = 0.35f; leg_back = -0.3f;
    }
    draw_limb(&astro_leg, &base, v3_(-0.17f, -0.15f, 0), swing + leg_back, 0);
    draw_limb(&astro_leg, &base, v3_(0.17f, -0.15f, 0), -swing + leg_back, 0);
    draw_limb(&astro_arm, &base, v3_(-0.40f, 0.40f, 0), arm_swing, arm_splay);
    draw_limb(&astro_arm, &base, v3_(0.40f, 0.40f, 0), -arm_swing * (p->grounded ? 1 : -1), -arm_splay);

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

void render_scene(const world *w, const player *p, const camera *c, float time) {
    C3D_RenderTargetClear(top, C3D_CLEAR_ALL, 0x02020AFF, 0);
    C3D_FrameDrawOn(top);
    prepare_state();

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
    /* stars follow the camera so they never get closer */
    mtx_translate(&model, c->pos);
    C3D_CullFace(GPU_CULL_NONE);
    draw(&stars, &model, &mat_glow);
    C3D_CullFace(GPU_CULL_BACK_CCW);

    for (int i = 0; i < BODY_COUNT; i++) {
        mtx_translate(&model, w->b[i].pos);
        draw(&bodies[i], &model, w->b[i].emissive ? &mat_glow : &mat_lit);
        /* props are a few units tall: skip them once the planet is far enough that they
           would be smaller than a pixel anyway */
        float gap = v3_len(v3_sub(c->pos, w->b[i].pos)) - w->b[i].radius;
        if (props[i].count && gap < 700.0f) draw(&props[i], &model, &mat_lit);
    }
    draw_player(p, c, time);
}

void render_fini(void) {
    for (int i = 0; i < BODY_COUNT; i++) { gpu_free(&bodies[i]); gpu_free(&props[i]); }
    gpu_free(&astro_body); gpu_free(&astro_arm); gpu_free(&astro_leg);
    gpu_free(&flame_outer); gpu_free(&flame_inner); gpu_free(&stars);
    shaderProgramFree(&program);
    DVLB_Free(dvlb);
}
