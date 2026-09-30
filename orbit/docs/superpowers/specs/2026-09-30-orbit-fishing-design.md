# Orbit: fishing rod — design

Port of the Sound of Space (Unity) fishing mechanic to the Orbit 3DS demo. Numbers below are
taken from `FishingRodController.cs`, `Bobber.cs`, `FishFightSim.cs`, `FishingTuning.cs`,
`FishingTensionHUD.cs` and `RodBend.cs` in that project unless marked *3DS*. Units: Orbit world
units (the player is 1.8 tall, so ≈ metres) and seconds.

## Scope

- Equip/unequip the rod, charge and cast, bobber flight and landing, reel back with no fish,
  bite with a hook window, the tension fight, landing / snapping / slipping off, a visible fish,
  rod bend and haul, line, splash, tension bar, CAUGHT banner, a catch tally.
- Not included: audio, rumble, bait, time-of-day bite rates, inventory, fishdex, economy,
  ambient fish schools, multiplayer.

## Architecture

Four new units, three touched:

| Unit | Job | Depends on | 3DS headers |
|---|---|---|---|
| `fight.c/h` (new) | The tension sim, a port of `FishFightSim.Step(dt, holding)`. Owns tension, distance, stamina, runs, line tautness, outcome. | nothing but `vec.h` | none |
| `fishing.c/h` (new) | Everything else in the mechanic: equip state, charge, bobber physics, bite timer, strike window, fish approach/thrash positions, rod bend/haul angles, banner timer, tally. Drives `fight` while hooked. | `world`, `player`, `fight` | none |
| `fishhud.c/h` (new) | Top-screen citro2d overlay: tension bar, HOOK! prompt, CAUGHT banner. | `fishing`, `font` | citro2d |
| `render.c` | `draw_fishing()`: rod, line, bobber, fish, splash. New meshes: rod segment, bobber, fish. | `fishing` | citro3d |
| `hud.c` | Status word, controls block, tally line. | `fishing` | citro2d |
| `main.c` | Reads D-pad down and R, calls `fishing_update`, `fishhud_draw`. | | |
| `tests/test_fishing.c` (new) | Host tests for `fight` and `fishing`. | | none |

`fishing` and `fight` are plain C with no 3DS includes, like `player.c`, so `tests/run_tests.sh`
compiles them with the host gcc. `run_tests.sh` gets `source/fishing.c source/fight.c` added to its
compile line.

### Public interface

```c
/* fight.h */
typedef enum { FIGHT_ON, FIGHT_LANDED, FIGHT_SNAPPED, FIGHT_SLIPPED } fight_outcome;
typedef struct {
    int tier;                 /* 0 common, 1 uncommon, 2 rare */
    float weight_frac;        /* 0..1 within the tier's range */
    float tension;            /* 0..100 */
    float distance, start_distance;
    float stamina, stamina_start;
    float taut;               /* 0..1 line tautness */
    int running; float run_t, run_len, next_run;
    float slack_s, idle_s;
    float pull;               /* current pull, for the rod */
    fight_outcome outcome;
    uint32_t rng;
} fight;
void fight_start(fight *f, int tier, float weight_frac, float distance, float taut0, uint32_t seed);
void fight_step(fight *f, float dt, int holding);
static inline int fight_tight(const fight *f) { return f->taut >= 0.985f; }
static inline float fight_vigour(const fight *f) { return f->stamina_start > 0 ? f->stamina / f->stamina_start : 0; }
```

```c
/* fishing.h */
typedef enum {
    FISH_OFF,        /* rod not equipped */
    FISH_EQUIPPING,  /* 0.4 s swing up */
    FISH_HANGING,    /* bobber dangles under the tip, ready */
    FISH_CHARGING,   /* R held */
    FISH_CASTING,    /* wind-up + snap, bobber released part way */
    FISH_FLIGHT,     /* bobber in the air */
    FISH_GROUNDED,   /* bobber lying on land */
    FISH_WAITING,    /* bobber on water, bite timer runs */
    FISH_APPROACH,   /* fish spirals up (cosmetic, 5.4 s) */
    FISH_STRIKE,     /* hook window */
    FISH_FIGHT,      /* fight sim runs */
    FISH_TOWING,     /* landed: bobber towed over the shore to the tip */
    FISH_RETREAT,    /* fish flees after a miss / snap / slip (2.4 s) then back to WAITING */
    FISH_UNEQUIPPING /* 0.4 s swing down */
} fishing_state;

typedef struct {
    fishing_state state;
    float t;                  /* seconds in the current state */
    float charge;             /* 0..1 */
    float draw_t;             /* rod wind-up 0..1 */
    v3 bobber, bobber_vel;    /* world position / velocity (relative to the dominant body) */
    int bobber_visible, line_visible;
    float leash;              /* hanging: length of line below the tip */
    float retrieve_taut;      /* 0..1 outside a fight */
    int reeling;              /* R held and the retrieve is active */
    int r_released_since_cast;
    float bite_timer;         /* seconds until the fish arrives */
    float lost_bite_bonus;    /* 1 or 1.3 */
    int tier; float weight_frac;
    float strike_window;
    v3 fish_pos, fish_fwd; float fish_yaw; int fish_visible;
    fight fight;
    float rod_bend, rod_haul, rod_pose;  /* degrees: tip bend, whole-rod pull back, cast/equip pose */
    float bar_shake;                     /* HUD run tell, decays 3.5/s */
    float banner_t;                      /* CAUGHT banner seconds left */
    char banner[32];
    int caught;                          /* tally */
    v3 splash_pos; float splash_t;
    uint32_t rng;
} fishing;

typedef struct { int equip_pressed, r_down, r_pressed; v3 cast_dir; } fishing_input;

void fishing_init(fishing *f);
/* Once per frame after player_update. Reads the rod tip from player + camera basis. */
void fishing_update(fishing *f, const world *w, const player *p, const fishing_input *in, float dt);
/* World position of the rod tip (bent) and butt, for rendering and the line. */
void fishing_rod_points(const fishing *f, const player *p, v3 *butt, v3 *tip);
static inline int fishing_active(const fishing *f) { return f->state != FISH_OFF; }
```

Rod geometry: the rod is 1.6 long, held in the right hand. Its unbent butt is at the right hand
(model space `(0.40, -0.34, 0)` → world through `body_up/body_fwd`), pointing forward and up
(`fwd·cos(35°) + up·sin(35°)`) plus the pose angle. `fishing_rod_points` applies pose, haul and
bend so render and line agree. In first person the same points are used; the model is not drawn
but the rod is.

## Controls and state machine

All input is ignored unless `player_on_foot(p) && p->grounded && !p->swimming`. If the player
stops being grounded (jump, takeoff, falls in and swims) while the rod is out, the state goes
straight to `FISH_OFF`: bobber, line, fish and fight are cleared, no animation.

| State | Enter | Leave |
|---|---|---|
| OFF | | D-pad down → EQUIPPING |
| EQUIPPING | `rod_pose` eases from −120° to 0 over 0.4 s. Bobber glued to tip, no line. | t ≥ 0.4 → HANGING (leash 0.02) |
| HANGING | Leash grows to 0.75 at 1.4/s. Bobber springs to one leash below the tip (stiffness 45, damping 7/s, cap 20/s). Line drawn. | R pressed → CHARGING. D-pad down → UNEQUIPPING |
| CHARGING | `charge = clamp01(t / 2)`. `draw_t += dt / 0.15` (cap 1). `rod_pose = −(50·draw_t + 18·charge)`. Bobber offset from the tip shrinks to 0 with smoothstep over 0.22 s; line hidden. | R released → CASTING. D-pad down → UNEQUIPPING (charge lost) |
| CASTING | Finish draw over `0.15·(1 − draw_t)`; hold 0.02; snap to +5° over 0.1 s; release the bobber at 70 % of the snap; settle to 0 over 0.1 s. | Bobber released → FLIGHT (rod finishes settling on its own) |
| FLIGHT | `bobber_vel = p->vel + cast_dir·speed`, `speed = sqrt(lerp(11², 24², charge))` *(3DS, tuned so tap ≈ 4, full ≈ 14 on Verdant)*. Gravity `world_gravity_at − world_frame_accel(dom)`. Line drawn, slack. | Below sea radius on a liquid body → WAITING (splash, seat at sea_r − 0.01). Below terrain → GROUNDED |
| GROUNDED | Bobber sits on the terrain. | R held (after one release since the cast): `retrieve_taut` comes tight, then → TOWING (see Reeling). D-pad down → UNEQUIPPING |
| WAITING | Bobber seated, dip `((sin(1.2·t) − 1)·0.015)` below the seat, approaches rest at 0.6/s. `bite_timer` counts down (see Bite). | R held (after one release since the cast) → reeling, still WAITING. Reel arrives → TOWING (no fish) → HANGING. Timer ≤ 0 → APPROACH |
| APPROACH | 5.4 s cosmetic fish path (see Fish). Bite timer paused. | t ≥ 5.4 → STRIKE |
| STRIKE | Window 3 / 2 / 1.5 s by tier. Bobber submerged 0.12, shiver, wander. HOOK! prompt. | R pressed, or R already held while reeling → FIGHT. Timeout → RETREAT (lost_bite_bonus = 1.3) |
| FIGHT | `fight_start(distance = max(surface dist, 2.2), taut0 = retrieve_taut)`. Each frame `fight_step(dt, r_down)`; bobber submerged 0.28, moved along the surface toward `fight.distance` (≤ 17/s), plus wander, jitter, tugs. `bar_shake = 1` when a run starts. | LANDED → TOWING (fish attached). SNAPPED → RETREAT (rod recoil +42° / 0.06 s, back over 0.45 s; bobber floats up 0.6/s). SLIPPED → RETREAT (lost_bite_bonus = 1.3) |
| TOWING | Bobber (and fish if landed) dragged toward the tip at 6.6/s over land/water, no gravity. | Within 1.1 of tip → HANGING; if a fish was on: `caught++`, banner "CAUGHT! <TIER> <w> KG" for 2.5 s |
| RETREAT | Fish flees 5.5 away and down (k² ease) over 2.4 s, then hides. Bobber rises to its seat. | t ≥ 2.4 → WAITING (new bite timer) |
| UNEQUIPPING | Bobber, line, fish, fight cleared at once. `rod_pose` eases to +180° over 0.4 s. | t ≥ 0.4 → OFF |

D-pad down toggles: any state other than OFF/UNEQUIPPING → UNEQUIPPING.

### Reeling with no fish (WAITING, GROUNDED)

- `reeling = r_down && r_released_since_cast`.
- `retrieve_taut` moves toward 1 at 1/0.45 per s while reeling, toward 0 at 1/1.2 per s otherwise.
- Nothing moves until `retrieve_taut ≥ 0.985`. Then the bobber's anchor slides along the surface
  (water: sea sphere; ground: terrain) toward the player at 6.6/s.
- Arrival: surface distance to the player ≤ 1.6, or (water) the point 0.35 ahead of the bobber is
  above the terrain, → TOWING.
- While reeling the bite timer runs 1.2× faster; a bite while reeling goes APPROACH → STRIKE as
  usual, and STRIKE hooks immediately if R is still held.

## Bite

- New timer on entering WAITING (and after RETREAT): `rand(5, 14)` s. Counts down at
  `1 × (reeling ? 1.2 : 1) × lost_bite_bonus`.
- Tier roll at timer end. Base weights common 70 / uncommon 25 / rare 5; the cast distance `d`
  (surface distance at the seat) shifts them: `rare += 10·k`, `uncommon += 15·k`,
  `common −= 25·k` with `k = clamp01((d − 4) / 10)`. *(3DS simplification of the Unity tier
  table.)* `weight_frac = rand(0,1)`.
- Weight for the banner: common 0.4–1.5, uncommon 1.5–4, rare 4–12 kg, lerped by weight_frac.

## Fight (`fight_step`)

Per tier: `base_pull` 1.0 / 1.4 / 1.8; `run_speed` 3.2 / 3.9 / 5.5; stamina range 5–7 /
7.8–11.8 / 15–23.5 s (lerp by weight_frac); `resist = max·(0.8 + 0.2·weight_frac)`, max 0.70 /
0.85 / 0.93, halved when spent; first run after rand(0.4,0.8) / (0.5,1.0) / (0.5,0.9); gap
between runs rand(1.5,2.6) / (1.6,2.7) / (1.25,2.1).

Each step, in order:

1. Runs (stamina > 0 only): when `next_run` expires start a run of `rand(1,2)·(0.15 + 0.85·vigour)`
   s. `ramp = smoothstep(run_t / 0.2)`. `pull = base_pull·(1 + 0.6·ramp)`.
2. Running: `distance += run_speed·ramp·dt` (cap `start_distance·1.6`), `stamina −= dt`.
3. Line: `any_pull = holding || running`. Rate `1/0.45` when pulling (`1/(0.45·0.35)` if holding
   into a run), `1/1.2` when slack. Target 1 if pulling else `vigour·hold_fade·0.85`,
   `hold_fade = 1 − clamp01(idle_s / 1)`; 0 if spent. `taut` moves toward the target.
4. Force, only when `holding && tight`: `scale = (0.38 + 0.22·ramp)·lerp(0.3, 1, vigour)`;
   `tension += 80·pull·scale·dt`; `stamina −= dt`; `distance −= 13·(1 − resist)·dt`.
   Holding but not tight: tension unchanged. Not holding: `tension −= 95·dt` (floor 0), and if
   tension is 0 and `taut ≤ 0.01`, `slack_s += dt`. `idle_s` counts seconds since R was last held.
5. Outcome: `tension ≥ 100` → SNAPPED; `distance ≤ 1.2` → LANDED; `slack_s ≥ 3` → SLIPPED.

Expected feel: steady hold on a rare snaps in ≈ 2.5 s, holding into a run ≈ 1 s, so you release
on runs; a common with alternating hold/release lands in 10–20 s.

## Fish

One low-poly mesh (`mesh_fish`): a flattened diamond body with a tail fin, ~0.5 long, scaled
0.6 / 1.0 / 1.5 by tier, grey-green with a pale belly, `mat_lit`.

- APPROACH: spawn at depth `clamp(bed_depth − 0.4, 1.2, 7)` on the far side of the float
  (bed_depth = sea_r − terrain radius under the bobber). Helix 1.4 laps over 4.5 s, radius
  2.8 → 0.65, depth rising (smoothstep from k = 0.25) to 0.5; level off 0.6 s (radius → 0.5,
  depth → 0.42); pounce 0.28 s (k² ease) radius → 0.02, depth → −0.04. Body faces along its
  motion, pitch clamped to 45°.
- STRIKE / FIGHT: mouth at the float (0.02 above the surface + 0.1·half length), body pointing
  away from the player, yaw `sin(phase)·lerp(4°, 30°, vigour)` (×1.5 running), phase rate
  `lerp(2, 10, vigour)` rad/s (×2.4 running), head pitched up 12°, +10° lean when running. In
  STRIKE use vigour = 1.
- RETREAT: flee 5.5 away from the player and dive, k² ease, 2.4 s, then hidden.
- TOWING with a fish: fish stays on the float.

Bobber motion in STRIKE/FIGHT: wander target re-picked every `rand(0.6, 1.8)` s (÷vigour in a
fight), lateral ±1, along ±0.7, moved at 0.5/s, position eases with `1 − e^(−22 dt)`. STRIKE
shiver: vertical `(sin(16 t)·0.5 − 0.35)·0.02`. FIGHT jitter: `0.035·lerp(0.2, 1, vigour)`
(×1.8 running) from summed sines (13.7 / 27.1 Hz lateral, 9.3 / 19.7 Hz along); tugs every
`rand(0.35, 1.1) / vigour` s, phase decays 5/s, `k = phase²`, down `0.09·k·lerp(0.35, 1, vigour)`,
lateral `sin(12·phase)·0.036·k`.

## Rod, line, bobber, splash (render)

- **Rod**: 4 stacked tapered boxes (`mesh_box`, brown, 1.6 total, 0.05 → 0.02 thick). Segment
  `i` (0 = butt) is rotated about the butt by `bend·((i + 1)/4)²` toward the bobber so the tip
  whips and the butt stays stiff. `bend` target `38·curve(load)`, `load = taut·(0.18 + 0.82·tension/100)`
  in a fight (×0.5 spent), `0.16·retrieve_taut` reeling, else 0; `curve(x)` linear to 0.5 at
  x = 0.45 then `0.5 + 0.5·((x − 0.45)/0.55)^1.5`; eased at 8/s loading, 16/s releasing.
  **Haul** (whole rod back): 18° fighting (+7° running), 7.2° reeling, only while R held,
  × `clamp01((taut − 0.65)/0.35)`; eased 4/s on, 8/s off. **Hookset**: on hook wait for
  taut ≥ 0.65 (max 1.5 s) then 25° back over 0.1 s, handed to the haul. Snap recoil as above.
- **Third person**: right arm raised (swing −1.2 rad, i.e. forward) while the rod is out.
  **First person**: rod drawn, model still hidden.
- **Line**: quadratic Bézier from the bent tip to the bobber, control = midpoint + local
  gravity dir × distance × `0.3·lerp(1, 0.05, taut)`, 12 segments, a camera-facing ribbon 0.02
  wide, white α 0.5 (a little brighter than Unity's 0.3 so it reads at 400×240). Built each frame
  into a small linear-alloc'd vertex buffer. Hidden while the bobber is glued to the tip.
- **Bobber**: sphere radius 0.12 (`mesh_sphere` 6×8), red top half / white bottom (two spheres
  slightly offset is simpler than a split mesh: red 0.12 at +0.03 up, white 0.11 at −0.03).
- **Splash**: on water entry, a ring of 8 thin triangles on the surface expanding 0.2 → 1.2 over
  0.5 s, white, fading; drawn in the translucent pass.

## HUD

`fishhud_draw(top, &fishing)` after `gimbal_draw`:

- **Tension bar** (FIGHT only, fades in/out at 9/s): centred, top edge at y = 150. Track 120×5
  black α 0.42, fill from the left `120·tension/100`, orange `0xFFC040` up to 0.75 then lerp to
  red `(240, 66, 46)` at 1.0. Four L brackets (7 px arms, 2 px thick) at the corners, orange,
  red past 0.75. Whole bar offset `sin(46 t)·3.2·bar_shake` px in x.
- **HOOK!** during STRIKE, orange, centred at y = 150, blinking 8 Hz.
- **CAUGHT!** banner: `banner` text at scale 2 centred at y = 60 while `banner_t > 0`, orange,
  fading over the last 0.5 s.

`hud.c`:
- Status word `FISHING` while the rod is out.
- Controls block while the rod is out: `R  HOLD: CAST / REEL`, `TAP R: HOOK A BITE`,
  `DPAD DOWN: PUT AWAY`. While the rod is off and on foot add `DPAD DOWN: FISHING ROD`.
- Tally `FISH CAUGHT  N` under the status lines.

## main.c

```c
fishing_input fin = { .equip_pressed = (kDown & KEY_DDOWN) != 0, .r_down = (kHeld & KEY_R) != 0,
                      .r_pressed = (kDown & KEY_R) != 0, .cast_dir = cam.view };
fishing_update(&fish, &w, &p, &fin, dt);   /* after player_update, before camera_update */
```
On SELECT respawn: `fishing_init`. R still rolls in flight; it is only read by fishing on foot.

## Testing (`tests/test_fishing.c`, host)

Fight:
- Rare, weight 1, steady hold: snaps between 1.5 and 4 s, outcome SNAPPED.
- Common, hold 0.8 s / release 0.6 s cycle: lands within 40 s, tension never reaches 100.
- Release from tension 60: reaches 0 within 0.7 s.
- Hold with no fish running and taut 0: tension stays 0 for the first 0.4 s (line not tight yet).
- Never hold after hooking: SLIPPED at about 3 s after the line goes slack.

State machine (with a real `world`, player spawned on Verdant, facing the sea from the shore):
- D-pad down → EQUIPPING → HANGING after 0.4 s; bobber ~0.75 below the tip after 1 s.
- Hold R 2 s, release: FLIGHT, then WAITING within 3 s with the bobber within 0.05 of the sea
  radius; surface distance 10–16. Tap: 2–6.
- Hold R in WAITING: bobber distance shrinks only after ~0.45 s; reaches HANGING eventually.
- Force `bite_timer = 0`: APPROACH → STRIKE at 5.4 s; no R for the window → RETREAT → WAITING.
- STRIKE + R press → FIGHT; fight lands (scripted alternating hold) → TOWING → HANGING with
  `caught == 1` and a banner.
- D-pad down during FLIGHT → UNEQUIPPING with no bobber/line/fish, OFF after 0.4 s.
- Jump while HANGING → OFF immediately.

Then `make` for the 3DS; feel tuning (cast speed, bar size, colours) is done on the console.
