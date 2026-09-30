/* Fishing rod: equip with the d-pad, hold R to cast, hold R to reel, tap R to hook a bite, then
   fight the fish on the tension sim in fight.h. Plain C, no 3DS headers. Positions are world
   space; velocities are relative to the player's dominant body, like the player's. */
#ifndef FISHING_H
#define FISHING_H
#include "world.h"
#include "player.h"
#include "fight.h"

typedef enum {
    FISH_OFF,        /* rod not equipped */
    FISH_EQUIPPING,  /* 0.4 s swing up */
    FISH_HANGING,    /* bobber dangles under the tip, ready */
    FISH_CHARGING,   /* R held */
    FISH_CASTING,    /* wind-up + snap; the bobber leaves part way */
    FISH_FLIGHT,     /* bobber in the air */
    FISH_GROUNDED,   /* bobber lying on land */
    FISH_WAITING,    /* bobber on water, bite timer runs */
    FISH_APPROACH,   /* fish spirals up (cosmetic) */
    FISH_STRIKE,     /* hook window */
    FISH_FIGHT,      /* fight sim runs */
    FISH_TOWING,     /* bobber (and any fish) dragged to the rod tip */
    FISH_RETREAT,    /* fish flees after a miss / snap / slip, then WAITING */
    FISH_UNEQUIPPING /* 0.4 s swing down */
} fishing_state;

typedef struct {
    fishing_state state;
    float t;                  /* seconds in the current state */
    float time;               /* seconds since init, for wobbles */
    float charge;             /* 0..1 */
    float draw_t;             /* rod wind-up 0..1 */
    float cast_draw, cast_from, cast_back;   /* cast timeline: seconds to finish the draw, pose angles */
    v3 hand, hand_fwd, hand_up;   /* where the rod is held, copied from the input */
    v3 bobber, bobber_vel, bobber_up;   /* bobber_up: away from the dominant body at the bobber */
    v3 anchor;                /* point on the sea the floating bobber is tied to */
    float bob_off;            /* bobber height above the anchor: 0 seated, negative submerged */
    v3 wander, wander_target; float wander_t;
    float tug_t, tug_phase;
    int bobber_visible, line_visible;
    float line_taut;          /* 0..1 for the line's sag */
    float leash;              /* hanging: length of line below the tip */
    float retrieve_taut;      /* 0..1 outside a fight */
    int reeling;              /* R held and the retrieve is active */
    int r_released_since_cast;
    float bite_timer;         /* seconds until the fish arrives */
    float lost_bite_bonus;    /* 1, or 1.3 after a lost bite this cast */
    int tier; float weight_frac;
    float strike_window;
    float spawn_depth;
    v3 fish_pos, fish_fwd, fish_up; float fish_phase, fish_scale; int fish_visible;
    v3 retreat_from, retreat_dir;
    int with_fish;            /* TOWING: a landed fish is on the line */
    int hookset_pending;
    fight fight;
    float rod_pose, rod_haul, rod_bend, rod_recoil;  /* degrees; see fishing_rod_frame */
    float tw_from, tw_to, tw_t, tw_len;              /* rod_pose tween */
    float recoil_t;                                  /* -1 idle, else seconds since the snap */
    float bar_shake;          /* HUD run tell, decays 3.5/s */
    float banner_t;           /* CAUGHT banner seconds left */
    char banner[32];
    float lost_t;             /* "FISH BROKE OFF!" seconds left: solid above 1, fading below */
    char lost_msg[24];
    int caught;               /* tally */
    v3 splash_pos; float splash_t;
    uint32_t rng;
} fishing;

typedef struct {
    int equip_pressed;        /* d-pad down went down this frame */
    int r_down, r_pressed;    /* R held / went down this frame */
    v3 cast_dir;              /* unit vector the camera looks along */
    v3 hand, hand_fwd, hand_up;   /* the rod's butt and the plane it swings in (unit vectors) */
} fishing_input;

/* The rod: butt position and unbent direction after pose and haul; bent about bend_axis
   through the butt by bend_rad * s^2 at fraction s along the rod. */
typedef struct { v3 butt, dir, bend_axis; float bend_rad; } rod_frame;

void fishing_init(fishing *f);
/* Once per frame after player_update and camera_update. */
void fishing_update(fishing *f, const world *w, const player *p, const fishing_input *in, float dt);
void fishing_rod_frame(const fishing *f, rod_frame *r);
/* Point on the bent rod at fraction s (0 butt .. 1 tip). */
v3 fishing_rod_point(const rod_frame *r, float s);
static inline int fishing_active(const fishing *f) { return f->state != FISH_OFF; }
const char *fishing_tier_name(int tier);

#endif
