# Orbit: barebones explorable solar system for the 3DS

A third-person homebrew game (libctru + citro3d) with a sun and three planets on circular
orbits. The player walks on planets and uses a jetpack to fly between them with no loading.
Lives in `orbit/`, built with the same MSYS2 devkitARM toolchain as `app/`.

## World
- Sun at the origin, radius 40, emissive. Touching it respawns the player.
- Three planets on circular coplanar orbits (XZ plane), kinematic: position is a function of
  time. Angular speed is `sqrt(MU_sun / d^3)` so orbital motion matches the sun's gravity.
- Medium scale: planets radius 10..16 at distances 200..520; walking around one takes
  30..40 s; a hop between neighbours takes 20..30 s; one orbit takes a few minutes.
- The player starts standing on the middle planet.

## Physics (player only; planets never react)
- Inverse-square gravity from every body.
- The dominant body is the one with the strongest pull at the player. Player velocity is
  stored relative to the dominant body; the player's position is carried along with the
  dominant body's motion each frame. When the dominant body changes the velocity is
  converted so world velocity is continuous. The tidal correction (other bodies' pull at
  the dominant body's centre) is subtracted when the dominant body is a planet.
- Surface contact: distance to centre < radius + half height snaps the player to the
  surface and removes inward velocity. Grounded players stay snapped until they jump.
- Speed relative to the dominant body is capped for stability.

## Controls
- Circle pad: walk (grounded, direct velocity with friction) or lateral thrust (airborne),
  both relative to the camera, projected onto the plane tangent to local "up".
- B: jump when grounded; upward thrust while held in the air.
- A: downward thrust while airborne. Y: air brake (bleeds relative velocity).
- L / R: orbit the camera around the player. START quits. SELECT respawns.
- The player model turns to face the movement direction.

## Camera
- Third person behind the player. Camera up eases toward local gravity up; camera forward
  eases toward the player's facing direction while moving. Pushed outside the dominant
  body's surface if it would clip.

## Rendering
- Top screen: citro3d, vertex shader from the fragment_light example extended with a
  vertex colour attribute, PICA fragment lighting with one point light at the sun.
- Planets: UV spheres, vertex colours in a two-tone checker per body. Sun: emissive
  material. Player: box body, head, orange flame while thrusting. Stars: a few hundred
  small triangles on a big sphere that follows the camera.
- Bottom screen: citro2d HUD with body name, grounded/flying, speed, altitude, controls.

## Code layout (`orbit/source/`)
`vec.h` maths, `world.c/h` bodies and orbits, `player.c/h` physics and input mapping,
`camera.c/h`, `mesh.c/h` mesh generation, `render.c/h` citro3d setup and drawing,
`hud.c/h`, `main.c`, `vshader.v.pica`. World, player and camera use no 3DS headers so
`orbit/tests/` can run them on the host.

## Testing
Host tests for gravity direction, surface snap, frame-switch velocity continuity and
orbit consistency. Then the 3dsx in Azahar, then a CIA via a copy of `app/build_cia.sh`.
