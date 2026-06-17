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
  window grid, chimney, hospital cross, and police badge are all drawn here.

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
- `budget_` regen rate, deploy costs (`Game::deployAt`).
- `safety_` dynamics (the `rate` formula) — controls difficulty.
- `goalTime_` (in `Game.h`) — how long you must survive.
- Scoring is computed where the win/lose states are set.
