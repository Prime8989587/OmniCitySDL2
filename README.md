# CristiVerse — LogOS Engine (SDL2 Edition)

A living-city simulation sandbox where thousands of autonomous agents roam,
trade, commit crimes, get arrested, and get healed — all on their own. You play
the **mayor**: keep the city safe long enough to win by deploying police and
healers while the simulation plays out.

This is a full redesign of the original single-file `cristiverse_sdl2.cpp`
prototype into a modular, polished, downloadable game. It uses **pure SDL2** —
no SDL_ttf / SDL_image / SDL_mixer and **no external asset files**. All graphics,
the bitmap font, and the sound effects are generated procedurally, so the whole
game ships as a single small executable plus `SDL2.dll`.

![CristiVerse gameplay](docs/screenshot.png)

---

## ✨ Features

- **Modular C++17 codebase** — config, math, font, rendering, simulation, UI,
  audio, and game-state are cleanly separated.
- **Procedural graphics** — buildings with roofs, lit windows, shadows, parks
  with trees, factory chimneys with rising smoke, a hospital and police station
  with markers, a road grid, and a day/night cycle.
- **Animations** — agent walk-bob and facing, action flash rings
  (rob / arrest / heal / fight), floating event text, drifting particles,
  smooth camera pan/zoom, and a fade-in.
- **Agent AI with 5 roles** — Civilians flee threats and crack under stress;
  Criminals hunt and rob; Police chase and arrest; Gangs fight police; Healers
  calm and heal. A spatial grid keeps neighbor queries fast for thousands of agents.
- **Built-in UI toolkit** — buttons, toggles, sliders, panels, a top HUD with
  live role counts, a selected-agent info panel, a minimap with a live camera
  viewport box, and a scrolling event log.
- **Game loop** — Menu → Playing → Paused → Game Over, settings + help screens,
  0.5×/1×/2×/4× speed control, a safety meter, a budget economy, deployable
  units, and win/lose conditions with scoring.
- **Procedural audio** — synthesized SFX (click, select, crime, arrest, deploy,
  win, lose) with a master volume. Fails gracefully on machines with no audio.
- **Config file** — `cristiverse.cfg` is read at launch and saved on exit; tune
  resolution, agent count, and quality without recompiling.

---

## 🎮 How to play

You are the mayor. The city simulates itself — your job is to keep **City Safety**
above zero until the **Survive** timer runs out.

- Crime and high stress drain City Safety. Police reduce crime (they arrest and
  rehabilitate criminals). Healers reduce stress.
- Deploying units costs **Budget**, which regenerates over time. Spend wisely.
- Survive the timer with safety above zero to **win**. Let it hit zero and the
  city is **lost**. Your score rewards arrests, heals, time survived, and
  remaining safety.

### Controls

| Input | Action |
|-------|--------|
| `W A S D` / Arrow keys / right-drag | Pan the camera |
| Mouse wheel | Zoom in/out |
| Left click | Select an agent (see its stats) |
| `1` | Toggle the **Police** deploy tool, then click the map to place |
| `2` | Toggle the **Healer** deploy tool, then click the map to place |
| `Tab` | Cancel the active deploy tool |
| `[` / `]` | Slower / faster simulation speed |
| `Space` | Pause / resume |
| `H` | Help screen |
| `R` | Generate a brand-new city |
| `Esc` | Back / pause / quit |

---

## ⬇️ Downloads (prebuilt)

Prebuilt **Windows** and **Linux** packages are produced automatically by GitHub
Actions:

- **Tagged releases:** see the [Releases](../../releases) page for attached
  `CristiVerse-Windows.zip` and `CristiVerse-Linux.tar.gz`.
- **Latest build of any commit:** open the **Actions** tab → the most recent
  *Build* run → download the artifacts at the bottom.

On Windows, unzip and run `CristiVerse.exe` (keep `SDL2.dll` next to it).
On Linux, extract and run `./run.sh`.

---

## 🔨 Building from source

### Linux

```bash
sudo apt-get install -y libsdl2-dev cmake g++   # Debian/Ubuntu
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/bin/CristiVerse
```

Or just run `scripts/build-linux.sh`.

### macOS

```bash
brew install sdl2 cmake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/bin/CristiVerse
```

### Windows (MSVC)

1. Download the SDL2 **VC** development package from
   <https://github.com/libsdl-org/SDL/releases> (e.g. `SDL2-devel-2.30.9-VC.zip`)
   and extract it somewhere.
2. Configure and build, pointing CMake at it:

   ```powershell
   cmake -S . -B build -A x64 -DCMAKE_PREFIX_PATH="C:\path\to\SDL2-2.30.9"
   cmake --build build --config Release
   ```

3. The build copies `SDL2.dll` next to `build\bin\Release\CristiVerse.exe`.

### Windows (MSYS2 / MinGW)

```bash
pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-cmake mingw-w64-x86_64-gcc
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

---

## ⚙️ Configuration

A `cristiverse.cfg` file is read from the working directory at startup and
written back on exit. See [`packaging/cristiverse.cfg`](packaging/cristiverse.cfg)
for all keys (resolution, agent/building counts, graphics toggles, audio). You can
also change most of these live from the in-game **Settings** screen.

---

## 🧩 Project layout

```
src/
  Config.{h,cpp}   Settings struct + config-file load/save
  Math.h           Vec2, RNG, easing, color helpers
  Font.{h,cpp}     Built-in 5x7 bitmap font (no SDL_ttf)
  Render.{h,cpp}   Camera, world<->screen, draw primitives
  Sim.{h,cpp}      Agents, buildings, particles, spatial grid, AI step
  UI.{h,cpp}       Immediate-mode widgets (button/toggle/slider/panel)
  Audio.{h,cpp}    Procedural SFX engine
  Game.{h,cpp}     State machine, input, world + UI rendering
  main.cpp         Entry point (+ headless --shot screenshot mode)
CMakeLists.txt     Cross-platform build
.github/workflows/ CI that builds + packages Windows & Linux
packaging/         Example config used in release zips
scripts/           Local build & package helpers
```

See [MODDING.md](MODDING.md) to tweak the simulation, colors, or building types.

---

## 📷 Generating a screenshot (headless)

The executable can render a frame without a window — handy for CI smoke tests:

```bash
./build/bin/CristiVerse --shot out.bmp 200   # simulate 200 frames, save BMP
```
