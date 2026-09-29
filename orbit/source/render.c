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

typedef struct { void *vbo; int count; } gpu_mesh;

static C3D_RenderTarget *top;
static DVLB_s *dvlb;
static shaderProgram_s program;
static int uProj, uMV;
static C3D_Mtx proj, view;
static C3D_LightEnv lightEnv;
static C3D_Light light;
static C3D_LightLut lutPhong;

static gpu_mesh bodies[BODY_COUNT], body_box, head_box, visor_box, pack_box, flame_box, stars;

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
    gpu_mesh g;
    g.count = m.count;
    g.vbo = linearAlloc(m.count * sizeof(vertex));
    memcpy(g.vbo, m.v, m.count * sizeof(vertex));
    mesh_free(&m);
    return g;
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

    for (int i = 0; i < BODY_COUNT; i++) {
        const body *b = &w->b[i];
        int rings = i == BODY_SUN ? 24 : 16, segs = i == BODY_SUN ? 32 : 24;
        bodies[i] = upload(mesh_sphere(b->radius, rings, segs, b->color_a, b->color_b));
    }
    /* player: model +z is forward, +y is up, origin at the middle of the 1.8 tall body */
    body_box = upload(mesh_box(0.8f, 1.3f, 0.5f, 0xE8E8F0FF));
    head_box = upload(mesh_box(0.5f, 0.45f, 0.5f, 0xF0C8A0FF));
    visor_box = upload(mesh_box(0.36f, 0.16f, 0.1f, 0x203050FF));
    pack_box = upload(mesh_box(0.6f, 0.8f, 0.3f, 0x808890FF));
    flame_box = upload(mesh_box(0.35f, 0.7f, 0.35f, 0xFF9020FF));
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
    C3D_DrawArrays(GPU_TRIANGLES, 0, g->count);
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

static void draw_player(const player *p, const camera *c) {
    /* the model uses the camera's smoothed up so both roll together when "up" changes */
    v3 up = c->up;
    v3 fwd = v3_tangent(p->facing, up);
    fwd = v3_len(fwd) < 1e-3f ? v3_any_perp(up) : v3_norm(fwd);
    v3 right = v3_cross(up, fwd);
    C3D_Mtx base, m, off;
    mtx_basis(&base, right, up, fwd, p->pos);

    mtx_translate(&off, v3_(0, -0.25f, 0));  Mtx_Multiply(&m, &base, &off); draw(&body_box, &m, &mat_lit);
    mtx_translate(&off, v3_(0, 0.67f, 0));   Mtx_Multiply(&m, &base, &off); draw(&head_box, &m, &mat_lit);
    mtx_translate(&off, v3_(0, 0.70f, 0.28f)); Mtx_Multiply(&m, &base, &off); draw(&visor_box, &m, &mat_lit);
    mtx_translate(&off, v3_(0, -0.15f, -0.38f)); Mtx_Multiply(&m, &base, &off); draw(&pack_box, &m, &mat_lit);
    if (p->thrusting) {
        mtx_translate(&off, v3_(0, -0.85f, -0.38f)); Mtx_Multiply(&m, &base, &off); draw(&flame_box, &m, &mat_glow);
    }
}

void render_scene(const world *w, const player *p, const camera *c) {
    C3D_RenderTargetClear(top, C3D_CLEAR_ALL, 0x02020AFF, 0);
    C3D_FrameDrawOn(top);
    prepare_state();

    Mtx_PerspTilt(&proj, C3D_AngleFromDegrees(55.0f), C3D_AspectRatioTop, 0.5f, 3.0f * STAR_RADIUS, false);
    Mtx_LookAt(&view, FVec3_New(c->pos.x, c->pos.y, c->pos.z),
               FVec3_New(c->target.x, c->target.y, c->target.z),
               FVec3_New(c->up.x, c->up.y, c->up.z), false);
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
    }
    draw_player(p, c);
}

void render_fini(void) {
    for (int i = 0; i < BODY_COUNT; i++) linearFree(bodies[i].vbo);
    linearFree(body_box.vbo); linearFree(head_box.vbo); linearFree(visor_box.vbo);
    linearFree(pack_box.vbo); linearFree(flame_box.vbo); linearFree(stars.vbo);
    shaderProgramFree(&program);
    DVLB_Free(dvlb);
}
