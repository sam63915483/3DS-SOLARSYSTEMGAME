#include "minitest.h"
#include "world.h"
#include "village.h"
#include "player.h"
#include "mesh.h"
#include <string.h>

#define DT (1.0f / 60.0f)

int main(void) {
    world w; world_init(&w);
    const village *v = &w.village;
    const body *b = &w.b[BODY_VERDANT];
    CHECK(v->n == HOUSE_N && v->body == BODY_VERDANT, "two houses stand on Verdant");
    v3 spawn = world_spawn_dir(&w, BODY_VERDANT);
    for (int k = 0; k < v->n; k++) {
        const house *h = &v->h[k];
        float dist = acosf(v3_dot(h->dir, spawn)) * b->radius;
        printf("  -- house %d: %.0f units from the spawn, pad height %.1f\n", k, dist, h->pad_h);
        CHECK(dist > 8 && dist < 60, "a short walk from the spawn");
        CHECK(h->pad_h > b->sea_level + 0.5f, "on dry land");
        /* the pad is flat: the terrain within 7 units is at the pad height */
        v3 side = v3_any_perp(h->dir);
        int flat = 1;
        for (int a = 0; a < 8; a++) {
            v3 d = v3_norm(v3_add(h->dir, v3_scale(v3_rotate(side, h->dir, a * 0.785f), 7.0f / b->radius)));
            if (fabsf(world_height(&w, BODY_VERDANT, d) - h->pad_h) > 0.5f) flat = 0;
        }
        CHECK(flat, "the ground around it is flat");
        CHECK(fabsf(v3_dot(h->fwd, h->dir)) < 1e-3f, "the door faces along the ground");
        /* no props or grass on the pad */
        int crowd = 0;
        for (int i = 0; i < b->nprops; i++) if (acosf(v3_dot(b->props[i].dir, h->dir)) * b->radius < 16.0f) crowd++;
        CHECK(crowd == 0, "no trees or rocks on the pad");
        /* walk in through the door, then into the back wall: you stop inside, never pass through */
        v3 origin, right, up, fwd;
        village_frame(&w, k, &origin, &right, &up, &fwd);
        player p; memset(&p, 0, sizeof p);
        p.dom = BODY_VERDANT;
        float door_z = k == 0 ? DOME_R : LODGE_HZ;
        p.pos = v3_add(v3_add(origin, v3_scale(fwd, door_z + 4.0f)), v3_scale(up, PLAYER_HALF_HEIGHT));
        p.up = p.body_up = up; p.facing = p.body_fwd = v3_scale(fwd, -1.0f); p.grounded = 1;
        player_input in; memset(&in, 0, sizeof in);
        in.cam_forward = in.view_forward = p.facing; in.view_up = up; in.my = 1;
        int entered = 0;
        for (int i = 0; i < 300; i++) {
            player_update(&p, &w, &in, DT);
            in.cam_forward = in.view_forward = v3_scale(fwd, -1.0f);
            v3 l = v3_sub(p.pos, origin);
            float z = v3_dot(l, fwd), x = v3_dot(l, right);
            if (z < door_z - 0.5f && fabsf(x) < 2.0f) entered = 1;
        }
        v3 l = v3_sub(p.pos, origin);
        float z = v3_dot(l, fwd);
        printf("     (ended %.1f behind the door line; entered=%d)\n", door_z - z, entered);
        CHECK(entered, "walking at the door takes you inside");
        CHECK(z > -door_z - 0.2f, "the back wall stops you: never out the far side");
        CHECK(p.grounded, "still on your feet inside");
        /* stand by the alien: it speaks and turns to you */
        v3 npc = v3_add(v3_add(v3_add(origin, v3_scale(right, v->h[k].npc_local.x)), v3_scale(up, PLAYER_HALF_HEIGHT)), v3_scale(fwd, v->h[k].npc_local.z + 1.5f));
        p.pos = npc;
        CHECK(village_talk(&w, &p) != NULL, "the alien has something to say when you are close");
        for (int i = 0; i < 120; i++) village_update(&w, &p, DT);
        v3 to = v3_norm(v3_tangent(v3_sub(p.pos, v3_sub(npc, v3_scale(fwd, 1.5f))), up));
        CHECK(v3_dot(w.village.h[k].npc_fwd, to) > 0.9f, "and turns to face you");
        p.pos = v3_add(npc, v3_scale(fwd, 20.0f));
        CHECK(village_talk(&w, &p) == NULL, "quiet again once you leave");
    }
    /* the meshes exist and differ */
    mesh h0 = mesh_house(0), h1 = mesh_house(1), a0 = mesh_alien(0), a1 = mesh_alien(1);
    CHECK(h0.count > 200 && h1.count > 200 && h0.count != h1.count, "two different houses");
    CHECK(a0.count > 100 && a1.count > 100 && a0.count != a1.count, "two different aliens");
    mesh_free(&h0); mesh_free(&h1); mesh_free(&a0); mesh_free(&a1);
    MT_END();
}
