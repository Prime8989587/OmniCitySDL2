# Modding & Tuning CristiVerse

Everything is plain C++17 with no asset files, so "modding" means editing a few
well-marked spots and rebuilding. Here are the most useful knobs.

## Quick balance changes (no recompile)

Edit `cristiverse.cfg` (created next to the executable). You can change the
world size, number of agents/buildings, graphics toggles, and audio. Most of
these are also exposed in the in-game **Settings** screen.

## Agent behaviour — `src/Sim.cpp`

The whole simulation lives in `World::step()`. Key tunables near the top:

```cpp
const float MAXSPEED = 46.0f;   // agent top speed (world units/sec)
const float FEAR_R   = 90.0f;   // how far civilians sense threats
const float HUNT_R   = 150.0f;  // how far hunters/police look for targets
const float ACT_R    = 24.0f;   // contact distance for rob/arrest/heal
```

Each role has a `case` in the big `switch (a.role)`. To change what a role does
(e.g. make healers also reduce crime, or criminals form groups), edit that block.
Use `grid.query(x, y, radius, fn)` for fast neighbor lookups.

To add **a new role**:
1. Add it to `enum class Role` in `src/Sim.h` (before `COUNT`).
2. Add a name in `roleName()` and a color in `roleColor()` in `src/Sim.cpp`.
3. Give it spawn numbers in `World::generateAgents()`.
4. Add a behaviour `case` in `World::step()`.

## Buildings — `src/Sim.cpp` & `src/Game.cpp`

- Types live in `enum class BType` (`src/Sim.h`) with names in `btypeName()`.
- Generation/placement: `World::generateBuildings()` (`src/Sim.cpp`).
- Appearance: `Game::renderBuilding()` (`src/Game.cpp`) — colors per type, roof,
  window grid, chimney, hospital cross, and police badge are all drawn here. The
  window grid is computed in **world space** so zoom never changes the pattern.
- `kBuildingSolidFrac` (`src/Sim.h`) sets how much of a building (from the
  bottom) is solid; the top is walkable "behind" space. When an agent stands in
  that rear zone, the building is drawn semi-transparent (occlusion x-ray) — see
  the depth-sorted painter pass in `Game::renderWorld()`.

## Trees — `src/Sim.cpp` & `src/Game.cpp`

- `enum class TreeType` (`src/Sim.h`): Deciduous, Pine, Willow, Dead.
- Placement: `World::generateTrees()` (count from `numTrees` in the config).
- Appearance: `Game::renderTree()` (`src/Game.cpp`).

## Park decoration — ponds & flowers — `src/Sim.cpp` & `src/Game.cpp`

- `World::generateParkDecor()` populates every park zone with a water body and
  flower clusters. Results are stored in `world.waters` and `world.flowers`.
- **Water bodies** (`struct Water`): One per park, placed deterministically using
  seeded RNG. Rendered as a single solid blue ellipse (`Game::renderWater`) that
  scales with day/night brightness — no gradient, shoreline, or shimmer. Disable
  with the `water` config toggle.
- **Flowers** (`struct Flower`): 10–20 clusters per park. Each cluster has 3–5
  flowers in varied colors (red, yellow, purple, white, pink). Blocky pixel
  stem+square-bloom shapes. Disable with the `flowers` config toggle.
- Both are non-solid (agents pass through) and respond to day/night brightness.
  Both render *before* the depth-sorted entities so agents always appear in front.

## Pixel-art rendering — all of `src/Game.cpp`, `src/UI.cpp`, `src/Render.cpp`

The world is drawn in a cohesive retro pixel-art style — blocky shapes, hard
edges, and bold, saturated colors instead of smooth curves and gradients.

- **`draw::thickRect()`** (`src/Render.cpp`) draws an N-pixel-thick rectangle
  outline; it is the workhorse for chunky borders (buildings, UI panels, agent
  selection brackets).
- **Agents** (`renderAgent`): a little blocky figure — head square + body rect +
  two legs — snapped to a 2-pixel grid. Walking uses a 2-frame leg shuffle
  (no smooth sine bob). The selection bracket and action flash are square
  outlines (`thickRect`), not circles.
- **Buildings** (`renderBuilding`): bold base colors, a flat thick roof bar
  (no perspective), a chunky 2–4 × 2–4 window grid with solid lit/dark panes,
  and blocky type markers (square police badge, fat hospital cross). The outline
  is a hard 1–2 px `thickRect`.
- **Trees** (`renderTree`): iconic blocky silhouettes — deciduous = stacked
  squares, pine = stepped pyramid of rectangles, willow = canopy block with
  hanging strands, dead = bare line branches.
- **UI** (`src/UI.cpp`): panels/buttons/toggles use `fillRect` + `thickRect`
  (square corners); the slider knob is a small square block.
- Snapping to a 2-pixel grid is done per element (`sx = (sx/2)*2`) rather than
  with a global low-res render target, so the bitmap font and HUD stay crisp.

## Ground textures & grass — `src/Game.cpp` & `src/Math.h`

- `renderGround()` tiles the visible world into 22-unit cells in **world space**
  and paints a two-tone green **checkerboard** (`(hx+hy)&1`) with occasional
  darker stipple specks. The pattern comes from a stable spatial hash
  (`hash2i()`/`hashf()` in `Math.h`) so it never shifts when panning or zooming.
- Roads are thick dirt-colored paths (`draw::thickLine`); the world border is a
  3 px `thickRect`.
- Disable the procedural grass checker with the `grass` config toggle; it falls
  back to a flat lawn color.

## HUD entity counter — `src/Game.cpp`

- `Game::countVisibleEntities()` returns how many awake, on-screen agents are in
  the current camera view (reuses the same cull bounds as `renderAgent`).
- `renderHUD()` shows it as **ON-SCREEN N** in the accent colour, left of the
  clock/FPS/speed readout. The role-count chips are compact (scale-1) so they
  never collide with it even at 4-digit populations.

## Shadows — `src/Game.cpp`

- All shadows now use `Game::shadowAlpha()`, a brightness-responsive helper that
  returns ~0 at night and ~0.4 at midday (scales with `dayBrightness()`).
- **Building shadows**: Offset down-right by ~10% of building height, ~80% of
  building footprint. Always cast beneath the building (painter's algorithm).
- **Agent shadows**: Small ground ellipse (~70% agent width × ~33% agent height),
  offset down-right by 2–3 pixels, always beneath the agent's feet.
- **Tree shadows**: Ellipse beneath trunk, ~60% of tree canopy width.
- All shadows are drawn *before* the entity they belong to, so entities always
  render on top. Disable all shadows with the `shadows` config toggle.

## Day/night & sleep — `src/Sim.cpp` & `src/Game.cpp`

- `World::hourOfDay()` returns 0–24 (noon == brightest). Civilians head home and
  sleep between 22:00 and 06:00 (the `night` window in `World::step()`); change
  those thresholds to retime the city.
- `Game::windowColor()` maps the hour to the lit-window tint (night blue → midday
  white-yellow → dusk orange).
- `Game::dayBrightness()` returns a 0–1 brightness factor used to scale all colors
  and shadow alphas. Day/night brightness applies uniformly to grass, water,
  flowers, shadows, and all other visual elements.

## Colors & theme — `src/UI.h`

The UI palette (panel background, accent, text colors) is in the `ui` namespace
inline helpers. Agent/role colors are in `roleColor()` (`src/Sim.cpp`).

## The font — `src/Font.cpp`

`GLYPHS[64][7]` is a 5×7 bitmap covering ASCII 32–95 (space..`_`); lowercase maps
to uppercase. Each glyph is 7 rows; bit 4 is the leftmost pixel. Edit a row's
binary literal to reshape a character, or extend the table for more glyphs.

## Sound — `src/Audio.h`

`Audio::blip(freqHz, durSec, vol, wave)` synthesizes a tone (wave 0=sine,
1=square, 2=triangle). The named SFX (`click()`, `arrest()`, `win()`, …) are
just sequences of blips — add your own and call them from `src/Game.cpp`.

## Gameplay rules — `src/Game.cpp`

The mayor loop is in `Game::update()`:
- Bounties: `econ::kBountyArrest` / `econ::kBountyHeal` (`src/Sim.h`) pay out on
  each arrest/heal; the sim spawns the matching `+$` float, and `update()` awards
  the budget from stat deltas. Steady trickle, periodic tax, and the 500 cap also
  live here.
- Deploy costs are in `Game::deployAt` (and are free in Sandbox).
- `safety_` dynamics (the `rate` formula) — controls difficulty.
- `goalTime_` (in `Game.h`) — how long you must survive.
- Scoring: `Game::liveScore()` plus leftover safety on a win.

## Modes & sandbox — `src/Game.cpp`

`GameMode` (`src/Game.h`) switches between Survival and Sandbox. `startSandbox()`
sets up free play; the `Tool::Spawn*` values drive the sandbox spawners (keys
`3`–`7`), and `renderInfoPanel()` shows the live stat-editing sliders when a mode
is Sandbox.
