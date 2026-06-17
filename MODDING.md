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
  seeded RNG. Rendered as ellipses with gradient (darker center, sandy shoreline,
  light highlight). Shimmer animation syncs to `world_.dayTime` for subtle wave
  motion. Disable with the `water` config toggle.
- **Flowers** (`struct Flower`): 10–20 clusters per park. Each cluster has 3–5
  flowers in varied colors (red, yellow, purple, white, pink). Simple stem+petal
  shapes. Disable with the `flowers` config toggle.
- Both are non-solid (agents pass through) and respond to day/night brightness.
  Both render *before* the depth-sorted entities so agents always appear in front.

## Building 3D roofs — `src/Game.cpp`

- `renderBuilding()` now draws a trapezoid "ceiling" cap above the main roof.
  The cap is ~55% the brightness of the base color and narrower at the top
  (perspective effect). The cap height scales with building height (`r.h / 12`).
- The edge between the roof and wall is highlighted with a lighter line for
  clarity. Disable with the `shadows` toggle (they share the same toggle for now).

## Ground textures & grass — `src/Game.cpp` & `src/Math.h`

- `renderGround()` now tiles the visible world into 24-unit cells in **world space**.
  Each cell gets a pseudo-random brightness offset (±8%) derived from a stable
  spatial hash (`hashf()` in `Math.h`). The hash is deterministic so the pattern
  never shifts when panning or zooming.
- Subtle blade strokes appear on grass when zoomed in enough (`tr.w >= 12`).
- Disable procedural grass with the `grass` config toggle; falls back to a flat
  lawn color.

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
