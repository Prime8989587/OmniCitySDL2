#include "Game.h"
#include "Config.h"
#include "Font.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <fstream>

namespace cv {

static const float kSpeeds[] = {0.5f, 1.0f, 2.0f, 4.0f};
static const int   kNumSpeeds = 4;
static const int   HUD_H   = 48;
static const int   SIDE_W  = 308;

// Where the runtime config lives. On Android the working directory is not
// writable, so we use SDL's per-app preferences directory; elsewhere we keep
// the historical behavior of a file next to the executable.
static std::string configPath() {
#ifdef __ANDROID__
    char* pref = SDL_GetPrefPath("OmniVerse", "OmniVerse");
    if (pref) {
        std::string p = std::string(pref) + "omniverse.cfg";
        SDL_free(pref);
        return p;
    }
#endif
    return "omniverse.cfg";
}

// =====================================================================
// Lifecycle
// =====================================================================
bool Game::init() {
    setupMobile();                       // hints + mobile-tuned defaults (before init)
    settings().loadFromFile(configPath());
    auto& s = settings();

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init error: %s", SDL_GetError());
        return false;
    }

#ifdef __ANDROID__
    Uint32 flags = SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN;  // phone owns the size
#else
    Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    if (s.fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif
    win_ = SDL_CreateWindow("OmniVerse — LogOS Engine (SDL2)",
                            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            s.screenW, s.screenH, flags);
    if (!win_) { SDL_Log("CreateWindow: %s", SDL_GetError()); return false; }

    Uint32 rflags = SDL_RENDERER_ACCELERATED | (s.vsync ? SDL_RENDERER_PRESENTVSYNC : 0);
    ren_ = SDL_CreateRenderer(win_, -1, rflags);
    if (!ren_) {
        ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_SOFTWARE);
        if (!ren_) { SDL_Log("CreateRenderer: %s", SDL_GetError()); return false; }
    }
    SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");  // nearest-neighbour for crisp pixel-art

    // Load the player-supplied sprite art (buildings/roads/etc.). Missing art is
    // harmless: renderers fall back to the original procedural look.
    textures_.init(ren_);
    textures_.loadAll();

    // Physical drawable size (used to map normalized touch coords).
    SDL_GetRendererOutputSize(ren_, &winPxW_, &winPxH_);
#ifdef __ANDROID__
    // Render at a comfortable logical resolution so text stays legible and touch
    // targets stay finger-sized on high-DPI phones; SDL scales the geometry to
    // the physical screen. The canvas is forced landscape.
    {
        int pxLong  = std::max(winPxW_, winPxH_);
        int pxShort = std::max(1, std::min(winPxW_, winPxH_));
        const int targetH = 620;                          // logical short side
        int logH = targetH;
        int logW = (int)std::lround((double)targetH * pxLong / pxShort);
        logW = std::max(logW, 900);                        // sane minimum width
        SDL_RenderSetLogicalSize(ren_, logW, logH);
        s.screenW = logW; s.screenH = logH;
        logicalActive_ = true;
        SDL_Log("OmniVerse: %dx%d physical -> %dx%d logical", winPxW_, winPxH_, logW, logH);
    }
#endif

    audio_.init(); // soft-fails when no device

    adjustWorldForDepth();
    world_.regenerate();
    syncSpriteSizes();

    layoutView();
    fitCameraToWorld();
    state_ = GState::Menu;
    fade_ = 1.0f;
    return true;
}

// Touch builds: make finger events authoritative and lock orientation. The
// hints are harmless on platforms where they do not apply.
void Game::setupMobile() {
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");   // no synthetic mouse from touch
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");   // no synthetic touch from mouse
#ifdef __ANDROID__
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    // Defaults tuned for a steady 60fps on low-end phones (weak GPU/CPU).
    auto& s = settings();
    s.numAgents    = 700;
    s.numTrees     = 80;
    s.worldW       = 2000.0f;
    s.worldH       = 2000.0f;
    s.vsync        = true;
    s.fullscreen   = true;
#endif
}

bool Game::initHeadless() {
    auto& s = settings();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init(headless) error: %s", SDL_GetError());
        return false;
    }
    shotSurface_ = SDL_CreateRGBSurfaceWithFormat(0, s.screenW, s.screenH, 32,
                                                  SDL_PIXELFORMAT_ARGB8888);
    if (!shotSurface_) { SDL_Log("surface: %s", SDL_GetError()); return false; }
    ren_ = SDL_CreateSoftwareRenderer(shotSurface_);
    if (!ren_) { SDL_Log("software renderer: %s", SDL_GetError()); return false; }
    SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");  // nearest-neighbour for crisp pixel-art
    textures_.init(ren_);
    textures_.loadAll();
    adjustWorldForDepth();
    world_.regenerate();
    syncSpriteSizes();
    layoutView();
    fitCameraToWorld();
    return true;
}

void Game::captureFrames(const char* path, int frames) {
    startNewGame();
    fade_ = 0.0f;
    for (int i = 0; i < frames; ++i) {
        update(1.0f / 60.0f);
        render();
    }
    if (SDL_SaveBMP(shotSurface_, path) == 0)
        SDL_Log("Saved screenshot: %s", path);
    else
        SDL_Log("SaveBMP failed: %s", SDL_GetError());
}

void Game::shutdown() {
    if (win_) settings().saveToFile(configPath()); // only persist for real sessions
    audio_.shutdown();
    textures_.shutdown();                          // free sprites before renderer
    if (ren_) SDL_DestroyRenderer(ren_);
    if (shotSurface_) SDL_FreeSurface(shotSurface_);
    if (win_) SDL_DestroyWindow(win_);
    SDL_Quit();
}

void Game::run() {
    Uint64 prev = SDL_GetPerformanceCounter();
    const double freq = (double)SDL_GetPerformanceFrequency();
    while (running_) {
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)((now - prev) / freq);
        prev = now;
        if (dt > 0.1f) dt = 0.1f;      // clamp huge stalls
        fps_ = lerp(fps_, dt > 0 ? 1.0f / dt : 0.0f, 0.1f);

        handleEvents();
        update(dt);
        render();
    }
}

// =====================================================================
// Input
// =====================================================================
void Game::handleEvents() {
    in_.mousePressed = false;
    in_.mouseReleased = false;
    in_.wheel = 0;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            running_ = false;
            break;

        // Android sends these around app suspend/resume; pause the sim so we
        // don't burn battery (and the GL context) while in the background.
        case SDL_APP_WILLENTERBACKGROUND:
        case SDL_APP_DIDENTERBACKGROUND:
            if (state_ == GState::Playing) state_ = GState::Paused;
            break;

        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                if (logicalActive_) {
                    // Logical canvas is fixed; just refresh the physical size
                    // used to map normalized touch coordinates.
                    SDL_GetRendererOutputSize(ren_, &winPxW_, &winPxH_);
                } else {
                    settings().screenW = e.window.data1;
                    settings().screenH = e.window.data2;
                }
                layoutView();
            }
            break;

        case SDL_MOUSEMOTION:
            in_.mouseX = e.motion.x;
            in_.mouseY = e.motion.y;
            if (dragging_) {
                int dx = e.motion.x - lastMx_;
                int dy = e.motion.y - lastMy_;
                lastMx_ = e.motion.x; lastMy_ = e.motion.y;
                view_.cam.tx -= dx / view_.cam.zoom;
                view_.cam.ty -= dy / view_.cam.zoom;
                view_.cam.x = view_.cam.tx; view_.cam.y = view_.cam.ty; // immediate while dragging
            }
            break;

        case SDL_MOUSEBUTTONDOWN:
            in_.mouseX = e.button.x; in_.mouseY = e.button.y;
            if (e.button.button == SDL_BUTTON_LEFT) {
                in_.mouseDown = true; in_.mousePressed = true;
                worldClickAt(e.button.x, e.button.y, 14.0f);
            } else if (e.button.button == SDL_BUTTON_RIGHT ||
                       e.button.button == SDL_BUTTON_MIDDLE) {
                dragging_ = true; lastMx_ = e.button.x; lastMy_ = e.button.y;
            }
            break;

        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                in_.mouseDown = false; in_.mouseReleased = true;
            } else if (e.button.button == SDL_BUTTON_RIGHT ||
                       e.button.button == SDL_BUTTON_MIDDLE) {
                dragging_ = false;
            }
            break;

        case SDL_MOUSEWHEEL: {
            in_.wheel = e.wheel.y;
            if (state_ == GState::Playing) {
                if (e.wheel.y > 0) view_.cam.tzoom *= 1.12f;
                if (e.wheel.y < 0) view_.cam.tzoom /= 1.12f;
                view_.cam.tzoom = clampf(view_.cam.tzoom, 0.12f, 6.0f);
            }
            break;
        }

        case SDL_FINGERDOWN:
            onFingerDown(e.tfinger.fingerId, e.tfinger.x, e.tfinger.y);
            break;
        case SDL_FINGERMOTION:
            onFingerMotion(e.tfinger.fingerId, e.tfinger.x, e.tfinger.y);
            break;
        case SDL_FINGERUP:
            onFingerUp(e.tfinger.fingerId, e.tfinger.x, e.tfinger.y);
            break;

        case SDL_KEYDOWN: {
            SDL_Keycode k = e.key.keysym.sym;
            // Android's hardware/gesture Back maps to the same "step out" flow.
            if (k == SDLK_ESCAPE || k == SDLK_AC_BACK) {
                // "Back" walks one step out: Playing -> Paused -> Menu -> quit.
                if (state_ == GState::Playing)       state_ = GState::Paused;
                else if (state_ == GState::Paused)   { state_ = GState::Menu; fade_ = 0.6f; }
                else if (state_ == GState::Settings) {
                    settings().saveToFile(configPath());
                    state_ = prevState_;
                } else if (state_ == GState::Help)   state_ = prevState_;
                else if (state_ == GState::GameOver) state_ = GState::Menu;
                else if (state_ == GState::Menu)     running_ = false;
            }
            if (state_ == GState::Playing) {
                if (k == SDLK_SPACE) state_ = GState::Paused;
                if (k == SDLK_r)     { if (mode_ == GameMode::Sandbox) startSandbox(); else startNewGame(); }
                if (k == SDLK_1)     { tool_ = (tool_ == Tool::Police) ? Tool::None : Tool::Police; }
                if (k == SDLK_2)     { tool_ = (tool_ == Tool::Healer) ? Tool::None : Tool::Healer; }
                if (k == SDLK_TAB)   { tool_ = Tool::None; }
                if (k == SDLK_LEFTBRACKET)  setSpeed(speedIndex_ - 1);
                if (k == SDLK_RIGHTBRACKET) setSpeed(speedIndex_ + 1);
                if (k == SDLK_h)     { prevState_ = state_; state_ = GState::Help; }
                if (mode_ == GameMode::Sandbox) {
                    if (k == SDLK_3) tool_ = (tool_ == Tool::SpawnCivil)    ? Tool::None : Tool::SpawnCivil;
                    if (k == SDLK_4) tool_ = (tool_ == Tool::SpawnCriminal) ? Tool::None : Tool::SpawnCriminal;
                    if (k == SDLK_5) tool_ = (tool_ == Tool::SpawnPolice)   ? Tool::None : Tool::SpawnPolice;
                    if (k == SDLK_6) tool_ = (tool_ == Tool::SpawnHealer)   ? Tool::None : Tool::SpawnHealer;
                    if (k == SDLK_7) tool_ = (tool_ == Tool::SpawnGang)     ? Tool::None : Tool::SpawnGang;
                    if (k == SDLK_g) { showGrid_ = !showGrid_; }
                    if (k == SDLK_n) { world_.regenerateAgentsOnly(); selectedId_ = -1; toast("Population respawned"); }
                    // Placement editing shortcuts
                    const Uint8* ks2 = SDL_GetKeyboardState(nullptr);
                    if (k == SDLK_z && (ks2[SDL_SCANCODE_LCTRL] || ks2[SDL_SCANCODE_RCTRL])) undoLastAction();
                    if (k == SDLK_y && (ks2[SDL_SCANCODE_LCTRL] || ks2[SDL_SCANCODE_RCTRL])) redoLastAction();
                    if (k == SDLK_s && (ks2[SDL_SCANCODE_LCTRL] || ks2[SDL_SCANCODE_RCTRL])) {
                        std::string path = "layout.json";
#ifdef __ANDROID__
                        char* pref = SDL_GetPrefPath("OmniVerse", "OmniVerse");
                        if (pref) { path = std::string(pref) + "layout.json"; SDL_free(pref); }
#endif
                        saveLayout(path);
                    }
                    if (k == SDLK_l && (ks2[SDL_SCANCODE_LCTRL] || ks2[SDL_SCANCODE_RCTRL])) {
                        std::string path = "layout.json";
#ifdef __ANDROID__
                        char* pref = SDL_GetPrefPath("OmniVerse", "OmniVerse");
                        if (pref) { path = std::string(pref) + "layout.json"; SDL_free(pref); }
#endif
                        loadLayout(path);
                    }
                }
            } else if (state_ == GState::Paused) {
                if (k == SDLK_SPACE) state_ = GState::Playing;
            }
            break;
        }
        default: break;
        }
    }
}

// =====================================================================
// Touch input (mobile) — gesture recognition layered on SDL finger events.
//
// Design: SDL's touch->mouse synthesis is disabled on touch builds (see
// setupMobile), so finger events are the single source of truth. The primary
// finger mirrors into InputState so the immediate-mode UI (menus, sidebar,
// sliders) keeps working unchanged. The world view adds gestures on top:
//   * single-finger tap   -> select agent / deploy tool
//   * single-finger drag  -> pan camera
//   * two-finger pinch     -> zoom
// =====================================================================
namespace { constexpr float kTapSlop = 9.0f; }  // logical px before a tap becomes a drag

// Convert a normalized SDL finger coordinate (0..1 over the window) into the
// logical render-coordinate space the game draws in.
void Game::fingerToLogical(float nx, float ny, int& lx, int& ly) const {
    int px = (int)std::lround(nx * (winPxW_ > 0 ? winPxW_ : settings().screenW));
    int py = (int)std::lround(ny * (winPxH_ > 0 ? winPxH_ : settings().screenH));
#if SDL_VERSION_ATLEAST(2, 0, 18)
    if (logicalActive_) {
        float fx = 0.0f, fy = 0.0f;
        SDL_RenderWindowToLogical(ren_, px, py, &fx, &fy);  // accounts for letterbox
        lx = (int)std::lround(fx);
        ly = (int)std::lround(fy);
        return;
    }
#endif
    lx = px; ly = py;
}

void Game::onFingerDown(SDL_FingerID id, float nx, float ny) {
    int lx, ly; fingerToLogical(nx, ny, lx, ly);
    if (touchCount_ == 0) {
        // Primary finger: arm a potential tap and mirror to the UI cursor.
        f0Id_ = id; f0x_ = (float)lx; f0y_ = (float)ly;
        touchStartX_ = (float)lx; touchStartY_ = (float)ly;
        touchMoved_ = false;
        pinching_ = false;
        touchInWorld_ = (state_ == GState::Playing || state_ == GState::Paused) &&
                        lx < view_.viewX + view_.viewW && ly > view_.viewY;
        in_.mouseX = lx; in_.mouseY = ly;
        in_.mouseDown = true; in_.mousePressed = true;
        touchCount_ = 1;
    } else if (touchCount_ == 1) {
        // Second finger: start a pinch and cancel the pending tap / UI press.
        f1Id_ = id; f1x_ = (float)lx; f1y_ = (float)ly;
        pinching_ = true;
        touchMoved_ = true;          // never fire a tap once two fingers are down
        in_.mouseDown = false;       // abort any UI press the first finger started
        pinchLastDist_ = std::hypot(f1x_ - f0x_, f1y_ - f0y_);
        touchCount_ = 2;
    } else {
        touchCount_++;               // ignore 3rd+ fingers
    }
}

void Game::onFingerMotion(SDL_FingerID id, float nx, float ny) {
    int lx, ly; fingerToLogical(nx, ny, lx, ly);
    if (pinching_) {
        if      (id == f0Id_) { f0x_ = (float)lx; f0y_ = (float)ly; }
        else if (id == f1Id_) { f1x_ = (float)lx; f1y_ = (float)ly; }
        float dist = std::hypot(f1x_ - f0x_, f1y_ - f0y_);
        if (pinchLastDist_ > 1.0f && dist > 1.0f &&
            (state_ == GState::Playing || state_ == GState::Paused)) {
            float ratio = dist / pinchLastDist_;
            view_.cam.tzoom = clampf(view_.cam.tzoom * ratio, 0.12f, 6.0f);
        }
        pinchLastDist_ = dist;
        return;
    }
    if (id != f0Id_) return;
    float dx = (float)lx - f0x_, dy = (float)ly - f0y_;
    f0x_ = (float)lx; f0y_ = (float)ly;
    in_.mouseX = lx; in_.mouseY = ly;
    if (std::hypot((float)lx - touchStartX_, (float)ly - touchStartY_) > kTapSlop)
        touchMoved_ = true;
    if (touchInWorld_ && touchMoved_) {
        // One-finger drag over the map pans the camera (like right-drag).
        in_.mouseDown = false;       // it's a pan, not a UI press
        view_.cam.tx -= dx / view_.cam.zoom;
        view_.cam.ty -= dy / view_.cam.zoom;
        view_.cam.x = view_.cam.tx; view_.cam.y = view_.cam.ty;  // immediate
    }
    // Otherwise it's a UI drag (e.g. a slider): mouseDown stays held and the
    // mirrored mouseX/mouseY let the widget track the finger.
}

void Game::onFingerUp(SDL_FingerID id, float nx, float ny) {
    int lx, ly; fingerToLogical(nx, ny, lx, ly);
    if (pinching_) {
        // Only the two pinch fingers affect the gesture; a stray 3rd finger
        // lifting just decrements the count and is otherwise ignored.
        if (id == f0Id_ || id == f1Id_) {
            // Keep the finger that's still down as the primary, but never let it
            // become a tap or a pan after a pinch.
            if (id == f0Id_) { f0Id_ = f1Id_; f0x_ = f1x_; f0y_ = f1y_; }
            // (if f1 lifted, f0 already holds the remaining finger's state)
            touchMoved_ = true;
            touchInWorld_ = false;
            pinching_ = false;
            in_.mouseDown = false;
        }
        touchCount_ = std::max(0, touchCount_ - 1);
        return;
    }
    if (id == f0Id_) {
        in_.mouseX = lx; in_.mouseY = ly;
        if (!touchMoved_) {
            // A clean tap: let the UI fire this frame, then handle the world.
            in_.mouseReleased = true;
            in_.mouseDown = false;
            if (touchInWorld_) worldClickAt(lx, ly, 26.0f);  // finger-sized pick
        } else {
            in_.mouseDown = false;
        }
    }
    touchCount_ = std::max(0, touchCount_ - 1);
    if (touchCount_ == 0) in_.mouseDown = false;
}

// Shared primary-click handler: select an agent or deploy the active tool when
// the click falls inside the world view. Used by both mouse and touch taps.
void Game::worldClickAt(int sx, int sy, float pickPx) {
    bool inWorld = (state_ == GState::Playing) &&
                   sx < view_.viewX + view_.viewW && sy > view_.viewY;
    if (!inWorld) return;
    if (tool_ != Tool::None) {
        deployAt(sx, sy);
    } else {
        float wx, wy; view_.screenToWorld(sx, sy, wx, wy);
        // In Sandbox mode, check if clicking on a building to drag it
        if (mode_ == GameMode::Sandbox) {
            int buildingIdx = -1;
            for (int i = (int)world_.buildings.size() - 1; i >= 0; --i) {
                const Building& b = world_.buildings[i];
                if (wx >= b.pos.x && wx < b.pos.x + b.w &&
                    wy >= b.pos.y && wy < b.pos.y + b.h) {
                    buildingIdx = i;
                    break;  // top building (last in list)
                }
            }
            if (buildingIdx >= 0) {
                dragBuildingIdx_ = buildingIdx;
                inEditMode_ = true;
                previewPos_ = world_.buildings[buildingIdx].pos;
                return;
            }
        }
        int id = world_.pickAgentNear(wx, wy, pickPx / view_.cam.zoom);
        selectedId_ = id;
        if (id >= 0) audio_.select();
    }
}

// =====================================================================
// Update
// =====================================================================
void Game::setSpeed(int idx) {
    speedIndex_ = std::clamp(idx, 0, kNumSpeeds - 1);
    simSpeed_ = kSpeeds[speedIndex_];
}

// Fit camera to world: compute zoom so the entire world is visible with a 5%
// margin. Must be called AFTER layoutView() has set viewW/viewH.
void Game::fitCameraToWorld() {
    auto& s = settings();
    float zoomX = view_.viewW / s.worldW;
    float zoomY = view_.viewH / s.worldH;
    float zoom = std::min(zoomX, zoomY) * 0.95f;
    view_.cam.snap(s.worldW * 0.5f, s.worldH * 0.5f, zoom);
}

// Set world to a small fixed size (no longer depth-scaled). City Depth now
// controls building count instead. The small world ensures all ~20 buildings
// fit on screen at once, creating a dense, intimate neighborhood.
void Game::adjustWorldForDepth() {
    auto& s = settings();
    s.worldW = 480.0f;   // small fixed world, tuned for ~20 buildings visible at once
    s.worldH = 480.0f;
}

void Game::startNewGame() {
    auto& s = settings();
    mode_ = GameMode::Survival;
    adjustWorldForDepth();
    world_.regenerate();
    syncSpriteSizes();
    // Fit camera to show the whole world (accounting for HUD/sidebar at first render)
    float screenViewW = s.screenW - SIDE_W;  // width available for world view
    float screenViewH = s.screenH - HUD_H;   // height available for world view
    float zoomX = screenViewW / s.worldW;
    float zoomY = screenViewH / s.worldH;
    float zoom = std::min(zoomX, zoomY) * 0.95f;
    view_.cam.snap(s.worldW * 0.5f, s.worldH * 0.5f, zoom);
    selectedId_ = -1;
    budget_ = 200.0f;
    safety_ = 100.0f;
    gameTimer_ = 0.0f;
    won_ = false;
    finalScore_ = 0;
    tool_ = Tool::None;
    prevArrests_ = world_.stats.arrests;
    prevHeals_   = world_.stats.heals;
    incomeTimer_ = 0.0f;
    showGrid_ = false;
    setSpeed(1);
    state_ = GState::Playing;
    fade_ = 0.7f;
    toast("New city online. Keep it safe!");
}

void Game::startSandbox() {
    auto& s = settings();
    mode_ = GameMode::Sandbox;
    adjustWorldForDepth();
    world_.regenerate();
    syncSpriteSizes();
    // Fit camera to show the whole world (accounting for HUD/sidebar at first render)
    float screenViewW = s.screenW - SIDE_W;  // width available for world view
    float screenViewH = s.screenH - HUD_H;   // height available for world view
    float zoomX = screenViewW / s.worldW;
    float zoomY = screenViewH / s.worldH;
    float zoom = std::min(zoomX, zoomY) * 0.95f;
    view_.cam.snap(s.worldW * 0.5f, s.worldH * 0.5f, zoom);
    selectedId_ = -1;
    budget_ = 0.0f;            // unused — sandbox is free
    safety_ = 100.0f;
    gameTimer_ = 0.0f;
    won_ = false;
    finalScore_ = 0;
    tool_ = Tool::None;
    prevArrests_ = world_.stats.arrests;
    prevHeals_   = world_.stats.heals;
    incomeTimer_ = 0.0f;
    showGrid_ = false;
    setSpeed(1);
    state_ = GState::Playing;
    fade_ = 0.7f;
    toast("Sandbox: build your city freely.");
}

void Game::deployAt(int sx, int sy) {
    // Build tools place a structure instead of an agent.
    if (tool_ == Tool::BuildResidential || tool_ == Tool::BuildOffice ||
        tool_ == Tool::BuildIndustry    || tool_ == Tool::BuildPark) {
        placeBuildingAt(sx, sy);
        return;
    }
    Role  role; float cost;
    switch (tool_) {
        case Tool::Police:        role = Role::Police;   cost = 100.0f; break;
        case Tool::Healer:        role = Role::Healer;   cost = 80.0f;  break;
        case Tool::SpawnCivil:    role = Role::Civil;    cost = 0.0f;   break;
        case Tool::SpawnCriminal: role = Role::Criminal; cost = 0.0f;   break;
        case Tool::SpawnPolice:   role = Role::Police;   cost = 0.0f;   break;
        case Tool::SpawnHealer:   role = Role::Healer;   cost = 0.0f;   break;
        case Tool::SpawnGang:     role = Role::Gang;     cost = 0.0f;   break;
        default: return;
    }
    if (mode_ == GameMode::Sandbox) cost = 0.0f;
    if (mode_ == GameMode::Survival && budget_ < cost) {
        toast("Not enough budget!"); audio_.alarm(); return;
    }
    float wx, wy; view_.screenToWorld(sx, sy, wx, wy);
    Agent a;
    a.id = (int)world_.agents.size();
    a.pos = { wx, wy };
    a.role = role;
    a.stress = (role == Role::Criminal || role == Role::Gang) ? frand(0.4f, 0.7f)
                                                              : frand(0.1f, 0.3f);
    a.money  = frand(40.0f, 160.0f);
    a.animPhase = frand(0.0f, 6.28f);
    if (role == Role::Civil) a.home = world_.nearestHome(wx, wy);
    world_.agents.push_back(a);
    budget_ -= cost;
    audio_.deploy();
    world_.spawnBurst({wx, wy}, roleColor(role), 12, 90.0f);
    const char* verb = (mode_ == GameMode::Sandbox) ? "Spawned " : "Deployed ";
    world_.addLog(std::string(verb) + roleName(role) + ".", roleColor(role));
    toast(std::string(verb) + roleName(role));
}

// Custom building placement: drop a player-chosen structure at the clicked
// world point. Survival mode charges budget; sandbox is free. New structures
// can't overlap the solid footprint of an existing building (parks excepted,
// since they're walkable).
void Game::placeBuildingAt(int sx, int sy) {
    auto& s = settings();
    BType type; float cost;
    switch (tool_) {
        case Tool::BuildResidential: type = BType::Residential; cost = 50.0f;  break;
        case Tool::BuildOffice:      type = BType::Office;      cost = 80.0f;  break;
        case Tool::BuildIndustry:    type = BType::Industry;    cost = 100.0f; break;
        case Tool::BuildPark:        type = BType::Park;        cost = 30.0f;  break;
        default: return;
    }
    if (mode_ == GameMode::Sandbox) cost = 0.0f;
    if (mode_ == GameMode::Survival && budget_ < cost) {
        toast("Not enough budget!"); audio_.alarm(); return;
    }

    float wx, wy; view_.screenToWorld(sx, sy, wx, wy);

    // Fixed per-type footprint (deterministic — no random size).
    float bw, bh; buildingFootprint(type, bw, bh);
    float px, py;

    if (mode_ == GameMode::Sandbox) {
        // Free placement centered on cursor, rejected only on overlap / bounds.
        Vec2 outPos;
        if (!validateBuildingPlacement(wx, wy, type, -1, outPos, bw, bh)) {
            toast("Cannot place here"); audio_.alarm(); return;
        }
        px = outPos.x; py = outPos.y;
    } else {
        // Survival: clamp into bounds and reject overlap of solid footprints.
        px = clampf(wx - bw * 0.5f, 8.0f, s.worldW - bw - 8.0f);
        py = clampf(wy - bh * 0.5f, 8.0f, s.worldH - bh - 8.0f);
        for (const auto& ob : world_.buildings) {
            bool overlap = px < ob.pos.x + ob.w && px + bw > ob.pos.x &&
                           py < ob.pos.y + ob.h && py + bh > ob.pos.y;
            if (overlap) { toast("Too close to another building!"); audio_.alarm(); return; }
        }
    }

    Building b;
    b.pos = { px, py };
    b.w = bw; b.h = bh;
    b.type = type;
    b.variant = irand(0, 3);
    b.windowSeed = (unsigned)irand(1, 1 << 30);

    // Sync to actual sprite size (overrides the footprint default).
    SDL_Texture* tex = buildingTexture(b);
    if (tex) {
        int w = 0, h = 0;
        SDL_QueryTexture(tex, nullptr, nullptr, &w, &h);
        if (w > 0 && h > 0) {
            b.w = (float)w;
            b.h = (float)h;
        }
    }

    // Push undo snapshot before adding building (in Sandbox only)
    if (mode_ == GameMode::Sandbox) pushUndoSnapshot();

    world_.buildings.push_back(b);
    budget_ -= cost;
    audio_.deploy();
    world_.spawnBurst({ wx, wy }, {200, 210, 225, 255}, 14, 70.0f);
    world_.addLog(std::string("Built ") + btypeName(type) + ".", {200, 220, 255, 255});
    toast(std::string("Built ") + btypeName(type));
}

// =====================================================================
// Sandbox Placement Editing (free placement + footprint collision)
// =====================================================================

bool Game::buildingsOverlap(const Building& a, const Building& b) const {
    // Pure AABB footprint test — nothing may overlap (parks included) so the
    // editor packs cleanly.
    return !(a.pos.x + a.w <= b.pos.x || b.pos.x + b.w <= a.pos.x ||
             a.pos.y + a.h <= b.pos.y || b.pos.y + b.h <= a.pos.y);
}

// Validate a free (un-snapped) placement of `type` centered on the cursor.
// Writes the resulting footprint pos/size; returns false only if it would leave
// the world bounds. OVERLAP IS ALLOWED — buildings can nestle freely.
bool Game::validateBuildingPlacement(float wx, float wy, BType type,
                                     int, // skipIdx unused (was for drag validation)
                                     Vec2& outPos, float& outW, float& outH) {
    auto& s = settings();
    buildingFootprint(type, outW, outH);
    outPos = { wx - outW * 0.5f, wy - outH * 0.5f };   // center on cursor, free

    if (outPos.x < 8.0f || outPos.x + outW > s.worldW - 8.0f ||
        outPos.y < 8.0f || outPos.y + outH > s.worldH - 8.0f)
        return false;

    return true;
}

void Game::pushUndoSnapshot() {
    editBackup_.resize(editHistoryIndex_);  // discard redo history
    editBackup_.push_back(world_.buildings);  // snapshot current state
    editHistoryIndex_++;
}

void Game::undoLastAction() {
    if (editHistoryIndex_ <= 0) {
        toast("Nothing to undo");
        return;
    }
    editHistoryIndex_--;
    world_.buildings = editBackup_[editHistoryIndex_];
    toast("Undone");
    audio_.click();
}

void Game::redoLastAction() {
    if (editHistoryIndex_ >= (int)editBackup_.size()) {
        toast("Nothing to redo");
        return;
    }
    world_.buildings = editBackup_[editHistoryIndex_];
    editHistoryIndex_++;
    toast("Redone");
    audio_.click();
}

bool Game::saveLayout(const std::string& filename) {
    std::ofstream out(filename);
    if (!out.is_open()) { toast("Save failed"); return false; }

    out << "[\n";
    for (size_t i = 0; i < world_.buildings.size(); ++i) {
        const Building& b = world_.buildings[i];
        out << "  {\"type\": " << (int)b.type << ", \"x\": " << b.pos.x
            << ", \"y\": " << b.pos.y << ", \"w\": " << b.w << ", \"h\": " << b.h << "}";
        if (i + 1 < world_.buildings.size()) out << ",";
        out << "\n";
    }
    out << "]\n";
    out.close();
    toast("Layout saved");
    audio_.click();
    return true;
}

bool Game::loadLayout(const std::string& filename) {
    std::ifstream in(filename);
    if (!in.is_open()) { toast("Load failed"); return false; }

    world_.buildings.clear();
    std::string line, content;
    while (std::getline(in, line)) content += line;
    in.close();

    // Manual JSON parsing (minimal, no library dependency)
    size_t pos = content.find('[');
    if (pos == std::string::npos) { toast("Invalid layout file"); return false; }

    pos = content.find('{', pos);
    while (pos != std::string::npos) {
        Building b;
        size_t end = content.find('}', pos);
        if (end == std::string::npos) break;

        std::string obj = content.substr(pos, end - pos + 1);

        // Parse type
        size_t typePos = obj.find("\"type\": ");
        if (typePos != std::string::npos) {
            int typeVal = std::stoi(obj.substr(typePos + 8));
            b.type = (BType)typeVal;
        }

        // Parse x, y, w, h
        size_t xPos = obj.find("\"x\": ");
        if (xPos != std::string::npos) b.pos.x = std::stof(obj.substr(xPos + 5));

        size_t yPos = obj.find("\"y\": ");
        if (yPos != std::string::npos) b.pos.y = std::stof(obj.substr(yPos + 5));

        size_t wPos = obj.find("\"w\": ");
        if (wPos != std::string::npos) b.w = std::stof(obj.substr(wPos + 5));

        size_t hPos = obj.find("\"h\": ");
        if (hPos != std::string::npos) b.h = std::stof(obj.substr(hPos + 5));

        b.variant = irand(0, 3);
        b.windowSeed = (unsigned)irand(1, 1 << 30);
        world_.buildings.push_back(b);

        pos = content.find('{', end);
    }

    editHistoryIndex_ = 0;
    editBackup_.clear();
    pushUndoSnapshot();
    toast("Layout loaded");
    audio_.click();
    return true;
}

void Game::layoutView() {
    auto& s = settings();
    view_.screenW = s.screenW;
    view_.screenH = s.screenH;
    if (state_ == GState::Playing || state_ == GState::Paused) {
        view_.viewX = 0;
        view_.viewY = HUD_H;
        view_.viewW = s.screenW - SIDE_W;
        view_.viewH = s.screenH - HUD_H;
    } else {
        view_.viewX = 0; view_.viewY = 0;
        view_.viewW = s.screenW; view_.viewH = s.screenH;
    }
}

void Game::update(float dt) {
    view_.cam.update(dt);
    menuAnim_ += dt;
    if (toast_ > 0) toast_ -= dt;
    if (fade_ > 0) fade_ = approach(fade_, 0.0f, dt * 2.5f);

    if (state_ == GState::Playing) {
        layoutView();
        auto& s = settings();

        // Keyboard camera pan.
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        float camSpeed = 480.0f * dt / view_.cam.zoom;
        if (ks[SDL_SCANCODE_W] || ks[SDL_SCANCODE_UP])    view_.cam.ty -= camSpeed;
        if (ks[SDL_SCANCODE_S] || ks[SDL_SCANCODE_DOWN])  view_.cam.ty += camSpeed;
        if (ks[SDL_SCANCODE_A] || ks[SDL_SCANCODE_LEFT])  view_.cam.tx -= camSpeed;
        if (ks[SDL_SCANCODE_D] || ks[SDL_SCANCODE_RIGHT]) view_.cam.tx += camSpeed;

        // Sandbox placement editing: handle drag-to-move (free, collision-checked)
        if (mode_ == GameMode::Sandbox && inEditMode_ && dragBuildingIdx_ >= 0) {
            float wx, wy;
            view_.screenToWorld(in_.mouseX, in_.mouseY, wx, wy);
            BType dt2 = world_.buildings[dragBuildingIdx_].type;
            float pw, ph;
            bool valid = validateBuildingPlacement(wx, wy, dt2, dragBuildingIdx_,
                                                   previewPos_, pw, ph);
            previewValid_ = valid;

            // Release: finalize or revert
            if (in_.mouseReleased) {
                if (valid) {
                    pushUndoSnapshot();
                    world_.buildings[dragBuildingIdx_].pos = previewPos_;
                    toast("Building moved");
                    audio_.deploy();
                } else {
                    toast("Cannot place here");
                    audio_.alarm();
                }
                dragBuildingIdx_ = -1;
                inEditMode_ = false;
            }
        }

        // Fixed-step simulation, scaled by sim speed.
        simAccum_ += dt * simSpeed_;
        const float FIXED = 1.0f / 60.0f;
        int steps = 0;
        while (simAccum_ >= FIXED && steps < 10) {
            world_.step(FIXED);
            simAccum_ -= FIXED;
            steps++;
        }
        if (steps >= 10) simAccum_ = 0.0f;

        // Industry smoke.
        if (s.particles && s.animations) {
            for (auto& b : world_.buildings) {
                if (b.type != BType::Industry) continue;
                b.smokeAccum += dt;
                if (b.smokeAccum > 0.25f) {
                    b.smokeAccum = 0;
                    Particle p;
                    p.pos = { b.pos.x + b.w * 0.7f, b.pos.y + 4 };
                    p.vel = { frand(-6.0f, 6.0f), frand(-26.0f, -14.0f) };
                    p.maxLife = p.life = frand(1.5f, 2.6f);
                    p.size = frand(3.0f, 6.0f);
                    p.color = {90, 92, 100, 160};
                    if (world_.particles.size() < 2200) world_.particles.push_back(p);
                }
            }
        }

        gameTimer_ += dt * simSpeed_;

        // Economy: bounties from arrests/heals + steady city income.
        long aNow = world_.stats.arrests;
        long hNow = world_.stats.heals;
        if (mode_ == GameMode::Survival) {
            budget_ += (float)(aNow - prevArrests_) * econ::kBountyArrest;
            budget_ += (float)(hNow - prevHeals_)   * econ::kBountyHeal;
            budget_ += 5.0f * dt;                      // small steady trickle
            incomeTimer_ += dt * simSpeed_;
            if (incomeTimer_ >= 30.0f) {               // periodic city tax
                incomeTimer_ -= 30.0f;
                budget_ += 20.0f;
                toast("+$20 city tax");
            }
            budget_ = clampf(budget_, 0.0f, 500.0f);
        }
        prevArrests_ = aNow;
        prevHeals_   = hNow;

        // Safety + win/lose only matter in survival mode.
        if (mode_ == GameMode::Survival) {
            int living = 0;
            for (int i = 0; i < (int)Role::COUNT; ++i) living += world_.stats.count[i];
            int crime = world_.stats.count[(int)Role::Criminal] + world_.stats.count[(int)Role::Gang];
            float ratio = living > 0 ? (float)crime / living : 0.0f;
            float rate = 12.0f - ratio * 62.0f - std::max(0.0f, world_.stats.avgStress - 0.4f) * 34.0f;
            safety_ = clampf(safety_ + rate * dt, 0.0f, 100.0f);

            if (safety_ <= 0.0f) {
                won_ = false;
                finalScore_ = liveScore();
                state_ = GState::GameOver;
                audio_.lose();
            } else if (gameTimer_ >= goalTime_) {
                won_ = true;
                finalScore_ = liveScore() + (int)(safety_ * 3);
                state_ = GState::GameOver;
                audio_.win();
            }
        }

        // Building hover for tooltip.
        hoverBuilding_ = -1;
        if (in_.mouseX < view_.viewX + view_.viewW && in_.mouseY > view_.viewY) {
            float wx, wy; view_.screenToWorld(in_.mouseX, in_.mouseY, wx, wy);
            for (int i = 0; i < (int)world_.buildings.size(); ++i) {
                const Building& b = world_.buildings[i];
                if (wx >= b.pos.x && wx <= b.pos.x + b.w &&
                    wy >= b.pos.y && wy <= b.pos.y + b.h) { hoverBuilding_ = i; break; }
            }
        }
    }
}

// =====================================================================
// Day / night helpers
// =====================================================================
float Game::dayBrightness() const {
    if (!settings().dayNight) return 1.0f;
    float b = (std::sin(world_.dayTime * 6.2831853f - 1.5707963f) + 1.0f) * 0.5f;
    return 0.32f + 0.68f * b;
}
// Cast shadows are strongest under the midday sun and fade away at night.
float Game::shadowAlpha() const {
    if (!settings().shadows) return 0.0f;
    return 0.10f + 0.30f * dayBrightness();
}

// Count how many agents are currently visible on screen (for HUD display).
int Game::countVisibleEntities() const {
    int count = 0;
    for (const auto& a : world_.agents) {
        if (!a.alive || a.sleeping) continue;
        int sx, sy;
        view_.worldToScreen(a.pos.x, a.pos.y, sx, sy);
        float zoom = view_.cam.zoom;
        int rad = (int)clampf(zoom * 4.5f, 2.0f, 16.0f);
        if (sx < view_.viewX - 8 - rad && sx > view_.viewX + view_.viewW + 8 + rad) continue;
        if (sy < view_.viewY - 8 - rad && sy > view_.viewY + view_.viewH + 8 + rad) continue;
        count++;
    }
    return count;
}
SDL_Color Game::skyTop() const {
    float b = dayBrightness();
    return scaleColor({26, 36, 44, 255}, b);
}
SDL_Color Game::skyBottom() const {
    float b = dayBrightness();
    return scaleColor({14, 20, 26, 255}, b);
}

// Lit-window tint for the current hour: dark-blue at night, white-yellow at
// midday, orange in the evening, blending smoothly across dawn/dusk.
SDL_Color Game::windowColor() const {
    if (!settings().dayNight) return {255, 224, 140, 255};
    float h = world_.hourOfDay();
    SDL_Color night = {64, 86, 140, 255};    // dark blue
    SDL_Color day   = {255, 246, 205, 255};  // bright white-yellow
    SDL_Color dusk  = {255, 170, 92, 255};   // orange-yellow
    if (h >= 7.0f  && h < 17.0f) return day;
    if (h >= 5.0f  && h < 7.0f)  return lerpColor(night, day, (h - 5.0f) / 2.0f);
    if (h >= 17.0f && h < 18.0f) return lerpColor(day, dusk, h - 17.0f);
    if (h >= 18.0f && h < 19.0f) return lerpColor(dusk, night, h - 18.0f);
    return night; // 19:00 .. 05:00
}

int Game::liveScore() const {
    return (int)(world_.stats.arrests * 10 + world_.stats.heals * 5 + gameTimer_);
}

// =====================================================================
// Render — dispatch
// =====================================================================
void Game::render() {
    SDL_SetRenderDrawColor(ren_, 8, 10, 16, 255);
    SDL_RenderClear(ren_);

    switch (state_) {
    case GState::Menu:
        renderMenu();
        break;
    case GState::Playing:
    case GState::Paused:
        renderWorld();
        renderHUD();
        renderSidebar();
        renderHints();
        if (state_ == GState::Paused) renderPause();
        break;
    case GState::Settings:
        if (prevState_ == GState::Playing || prevState_ == GState::Paused) renderWorld();
        else renderMenu();
        renderSettingsScreen();
        break;
    case GState::Help:
        if (prevState_ == GState::Playing || prevState_ == GState::Paused) {
            renderWorld(); renderHUD(); renderSidebar();
        } else renderMenu();
        renderHelp();
        break;
    case GState::GameOver:
        renderWorld();
        renderGameOver();
        break;
    }

    renderToast();

    if (fade_ > 0.001f) {
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, (Uint8)(clampf(fade_, 0, 1) * 255));
        SDL_Rect full{0, 0, settings().screenW, settings().screenH};
        SDL_RenderFillRect(ren_, &full);
    }

    SDL_RenderPresent(ren_);
}

// =====================================================================
// World rendering
// =====================================================================
void Game::renderWorld() {
    SDL_Rect clip{ view_.viewX, view_.viewY, view_.viewW, view_.viewH };
    SDL_RenderSetClipRect(ren_, &clip);

    renderGround();
    renderParkDecor();
    if (showGrid_ && mode_ == GameMode::Sandbox) renderGrid();

    // Determine which buildings have an awake agent behind them => see-through.
    buildingOccluded_.assign(world_.buildings.size(), 0);
    for (size_t i = 0; i < world_.buildings.size(); ++i) {
        const Building& b = world_.buildings[i];
        if (b.type == BType::Park) continue;
        float cx = b.pos.x + b.w * 0.5f, cy = b.pos.y + b.h * 0.5f;
        float rad = std::max(b.w, b.h) * 0.6f;
        float rearBot = b.pos.y + b.h * (1.0f - kBuildingSolidFrac);
        bool occ = false;
        world_.grid.query(cx, cy, rad, [&](int j) {
            if (occ) return;
            const Agent& a = world_.agents[j];
            if (!a.alive || a.sleeping) return;
            if (a.pos.x >= b.pos.x && a.pos.x <= b.pos.x + b.w &&
                a.pos.y >= b.pos.y && a.pos.y <= rearBot)
                occ = true;
        });
        buildingOccluded_[i] = occ ? 1 : 0;
    }

    // Painter's pass: buildings, trees, and agents interleaved by base Y so
    // items lower on screen draw in front and higher ones draw behind.
    struct Item { float key; int type; int idx; }; // 0=building 1=tree 2=agent
    static std::vector<Item> items;
    items.clear();
    for (int i = 0; i < (int)world_.buildings.size(); ++i) {
        if (world_.buildings[i].type == BType::Park) continue; // lawns drawn in renderParkDecor
        items.push_back({ world_.buildings[i].pos.y + world_.buildings[i].h, 0, i });
    }
    for (int i = 0; i < (int)world_.trees.size(); ++i)
        items.push_back({ world_.trees[i].pos.y, 1, i });
    for (int i = 0; i < (int)world_.agents.size(); ++i) {
        const Agent& a = world_.agents[i];
        if (!a.alive || a.sleeping) continue;
        items.push_back({ a.pos.y, 2, i });
    }
    std::sort(items.begin(), items.end(),
              [](const Item& a, const Item& b) { return a.key < b.key; });
    for (const Item& it : items) {
        if (it.type == 0)      renderBuilding(world_.buildings[it.idx],
                                              buildingOccluded_[it.idx] ? (Uint8)175 : (Uint8)255);
        else if (it.type == 1) renderTree(world_.trees[it.idx]);
        else if (it.type == 2) renderAgent(world_.agents[it.idx]);
    }

    // Sandbox placement preview (green if valid, red if invalid)
    if (mode_ == GameMode::Sandbox && inEditMode_ && dragBuildingIdx_ >= 0) {
        const Building& b = world_.buildings[dragBuildingIdx_];
        SDL_Rect previewRect;
        if (view_.worldRectToScreen(previewPos_.x, previewPos_.y, b.w, b.h, previewRect)) {
            SDL_Color previewCol = previewValid_ ? SDL_Color{100, 200, 100, 120} : SDL_Color{200, 100, 100, 120};
            draw::fillRect(ren_, previewRect, previewCol);
            SDL_Color outlineCol = previewValid_ ? SDL_Color{100, 200, 100, 255} : SDL_Color{200, 100, 100, 255};
            draw::rect(ren_, previewRect, outlineCol);
        }
    }

    renderParticles();
    renderFloats();
    renderDayNight();

    // Building tooltip.
    if (state_ == GState::Playing && hoverBuilding_ >= 0 &&
        hoverBuilding_ < (int)world_.buildings.size()) {
        const Building& b = world_.buildings[hoverBuilding_];
        std::string label = btypeName(b.type);
        int tw = font::textWidth(label, 2);
        SDL_Rect tip{ in_.mouseX + 14, in_.mouseY + 14, tw + 16, 28 };
        ui::panel(ren_, tip);
        font::draw(ren_, label, tip.x + 8, tip.y + 7, 2, ui::textMain());
    }

    SDL_RenderSetClipRect(ren_, nullptr);
}

void Game::renderGround() {
    auto& s = settings();
    SDL_Rect v{ view_.viewX, view_.viewY, view_.viewW, view_.viewH };
    float b = dayBrightness();

    // Dark "void" beyond the world edges.
    draw::fillRect(ren_, v, scaleColor({18, 22, 24, 255}, b));

    // Visible world bounds (clamped to the actual world rect).
    float wx0, wy0, wx1, wy1;
    view_.screenToWorld(view_.viewX, view_.viewY, wx0, wy0);
    view_.screenToWorld(view_.viewX + view_.viewW, view_.viewY + view_.viewH, wx1, wy1);
    float gx0 = clampf(wx0, 0.0f, s.worldW), gy0 = clampf(wy0, 0.0f, s.worldH);
    float gx1 = clampf(wx1, 0.0f, s.worldW), gy1 = clampf(wy1, 0.0f, s.worldH);

    SDL_Color grassBase = scaleColor({58, 96, 54, 255}, b);
    if (!s.grass) {
        // Flat lawn fallback for low-end machines.
        SDL_Rect gr;
        if (view_.worldRectToScreen(gx0, gy0, gx1 - gx0, gy1 - gy0, gr))
            draw::fillRect(ren_, gr, grassBase);
    } else if (gx1 > gx0 && gy1 > gy0) {
        // Pixel-art grass: a two-tone checkerboard of world-space tiles with
        // occasional darker stipple specks. The pattern comes from a stable
        // spatial hash so it never shifts when panning or zooming, and the
        // checker reads as an obvious retro tiled texture.
        const float cell = 22.0f;
        SDL_Color grassDk = scaleColor({46, 82, 44, 255}, b);   // checker dark
        SDL_Color speck   = scaleColor({38, 70, 38, 255}, b);   // stipple
        float startX = std::floor(gx0 / cell) * cell;
        float startY = std::floor(gy0 / cell) * cell;
        for (float wy = startY; wy < gy1; wy += cell) {
            for (float wx = startX; wx < gx1; wx += cell) {
                float tx0 = std::max(wx, gx0), ty0 = std::max(wy, gy0);
                float tx1 = std::min(wx + cell, gx1), ty1 = std::min(wy + cell, gy1);
                SDL_Rect tr;
                if (!view_.worldRectToScreen(tx0, ty0, tx1 - tx0, ty1 - ty0, tr)) continue;
                if (tr.w <= 0 || tr.h <= 0) continue;
                int hx = (int)std::floor(wx / cell), hy = (int)std::floor(wy / cell);
                SDL_Color tile = ((hx + hy) & 1) ? grassBase : grassDk;  // checkerboard
                draw::fillRect(ren_, tr, tile);
                // Occasional darker stipple block for blocky texture.
                if (((hash2i(hx, hy) >> 7) & 3) == 0 && tr.w >= 4 && tr.h >= 4) {
                    int ss = std::max(2, tr.w / 3);
                    unsigned hs = hash2i(hx * 3 + 1, hy * 5 + 2);
                    int bx = tr.x + (int)(hs % (unsigned)std::max(1, tr.w - ss));
                    int by = tr.y + (int)((hs >> 8) % (unsigned)std::max(1, tr.h - ss));
                    draw::fillRect(ren_, { bx, by, ss, ss }, speck);
                }
            }
        }
    }

    // Roads are intentionally not drawn: they served no gameplay purpose and
    // cluttered the city. The road grid data still exists (buildRoadNetwork) so
    // cosmetic vehicles can drive, but nothing road-related is rendered here.

    // World border (chunky outline).
    SDL_Rect border;
    if (view_.worldRectToScreen(0, 0, settings().worldW, settings().worldH, border))
        draw::thickRect(ren_, border, 3, {90, 110, 140, 220});
}

// Tile the player's asphalt sprites into the same 200-unit road grid the dirt
// paths used. Straight tiles run along each road (rotated 90° for the vertical
// set) and the crossing sprite sits at every intersection. Returns false when
// the road art is missing so renderGround() can fall back to dirt lines.
bool Game::renderRoadSprites() {
    SDL_Texture* linear = textures_.get("AsphaltLinear.png");
    if (!linear) return false;
    SDL_Texture* inter = textures_.get("AsphaltParralel.png");
    if (!inter) inter = linear;

    const float step = roadSpacingForDepth(settings().cityDepth);  // depth-aware grid
    const float T    = 40.0f;    // square road-tile size in world units

    float wx0, wy0, wx1, wy1;
    view_.screenToWorld(view_.viewX, view_.viewY, wx0, wy0);
    view_.screenToWorld(view_.viewX + view_.viewW, view_.viewY + view_.viewH, wx1, wy1);

    auto blit = [&](SDL_Texture* t, float cx, float cy, double ang) {
        SDL_Rect dst;
        if (!view_.worldRectToScreen(cx - T * 0.5f, cy - T * 0.5f, T, T, dst)) return;
        if (dst.w <= 0 || dst.h <= 0) return;
        SDL_RenderCopyEx(ren_, t, nullptr, &dst, ang, nullptr, SDL_FLIP_NONE);
    };

    float gx0 = std::floor(wx0 / step) * step;
    float gy0 = std::floor(wy0 / step) * step;
    float tx0 = std::floor(wx0 / T) * T;
    float ty0 = std::floor(wy0 / T) * T;

    // Horizontal roads (constant y): tiles step along x.
    for (float y = gy0; y <= wy1; y += step)
        for (float x = tx0; x <= wx1; x += T)
            blit(linear, x + T * 0.5f, y, 0.0);
    // Vertical roads (constant x): tiles step along y, rotated a quarter turn.
    for (float x = gx0; x <= wx1; x += step)
        for (float y = ty0; y <= wy1; y += T)
            blit(linear, x, y + T * 0.5f, 90.0);
    // Crossings on top at each grid intersection.
    for (float y = gy0; y <= wy1; y += step)
        for (float x = gx0; x <= wx1; x += step)
            blit(inter, x, y, 0.0);
    return true;
}

// Park lawns, ponds, and flowers form a ground "decoration" layer drawn after
// the grass but before the depth-sorted entities, so agents and trees always
// pass in front of them.
void Game::renderParkDecor() {
    float bright = dayBrightness();

    // --- Park lawns (a slightly richer green than the surrounding grass). ---
    for (const auto& b : world_.buildings) {
        if (b.type != BType::Park) continue;
        SDL_Rect r;
        if (!view_.worldRectToScreen(b.pos.x, b.pos.y, b.w, b.h, r)) continue;
        SDL_Color lawn = scaleColor({74, 144, 74, 255}, (0.9f + 0.04f * b.variant) * bright);
        draw::fillRect(ren_, r, lawn);
        draw::thickRect(ren_, r, 2, scaleColor(lawn, 0.6f));
    }

    // --- Ponds, then flowers on top of the lawn. ---
    if (settings().water)
        for (const auto& w : world_.waters) renderWater(w);
    if (settings().flowers)
        for (const auto& f : world_.flowers) renderFlower(f);
}

void Game::renderWater(const Water& w) {
    int cx, cy;
    view_.worldToScreen(w.pos.x, w.pos.y, cx, cy);
    float zoom = view_.cam.zoom;
    int rx = (int)(w.rx * zoom), ry = (int)(w.ry * zoom);
    if (rx < 2 || ry < 2) return;
    if (cx + rx < view_.viewX || cx - rx > view_.viewX + view_.viewW ||
        cy + ry < view_.viewY || cy - ry > view_.viewY + view_.viewH) return;

    float bright = dayBrightness();

    // Simple solid blue water body, no shimmer or shoreline complexity.
    draw::fillEllipse(ren_, cx, cy, rx, ry, scaleColor({42, 112, 170, 255}, bright));
}

void Game::renderFlower(const Flower& f) {
    int sx, sy;
    view_.worldToScreen(f.pos.x, f.pos.y, sx, sy);
    if (sx < view_.viewX - 6 || sx > view_.viewX + view_.viewW + 6 ||
        sy < view_.viewY - 6 || sy > view_.viewY + view_.viewH + 6) return;
    float zoom = view_.cam.zoom;
    int pr = (int)clampf(f.size * zoom, 1.0f, 9.0f);
    float bright = dayBrightness();
    sx = (sx / 2) * 2; sy = (sy / 2) * 2;   // snap to pixel grid

    // Tiny blocky ground shadow.
    if (settings().shadows && pr >= 2)
        draw::fillRect(ren_, { sx - pr, sy - 1, pr * 2, std::max(1, pr / 2) },
                       {0, 0, 0, (Uint8)(shadowAlpha() * 90)});

    int stemH = std::max(2, (int)(f.size * 1.4f * zoom));
    SDL_Color stem = scaleColor({60, 132, 60, 255}, bright);
    draw::fillRect(ren_, { sx, sy - stemH, std::max(1, pr / 3), stemH }, stem);

    int headY = sy - stemH;
    SDL_Color petal = scaleColor(f.color, bright);
    if (pr <= 1) { draw::fillRect(ren_, { sx, headY, 1, 1 }, petal); return; }
    // Square bloom with a bright center pixel (blocky pixel flower).
    draw::fillRect(ren_, { sx - pr, headY - pr, pr * 2, pr * 2 }, petal);
    draw::fillRect(ren_, { sx - pr / 2, headY - pr / 2, std::max(1, pr), std::max(1, pr) },
                   scaleColor({252, 224, 120, 255}, bright));
}

void Game::renderGrid() {
    float wx0, wy0, wx1, wy1;
    view_.screenToWorld(view_.viewX, view_.viewY, wx0, wy0);
    view_.screenToWorld(view_.viewX + view_.viewW, view_.viewY + view_.viewH, wx1, wy1);

    // Vertical reference columns only, spaced at the selected building's footprint
    // width (the "4" in 4xH). The grid is a visual ruler — placement is free, so
    // these lines never constrain where a building goes. No horizontal lines
    // because Y is freeform.
    BType selType = BType::Residential;
    switch (tool_) {
        case Tool::BuildOffice:   selType = BType::Office;   break;
        case Tool::BuildIndustry: selType = BType::Industry; break;
        case Tool::BuildPark:     selType = BType::Park;     break;
        default:                  selType = BType::Residential; break;
    }
    float fw, fh; buildingFootprint(selType, fw, fh);
    float columnStep = std::max(4.0f, fw);
    SDL_Color columnColor{90, 150, 200, 80};
    float startX = std::floor(wx0 / columnStep) * columnStep;
    for (float x = startX; x <= wx1; x += columnStep) {
        int sx, sy, sx2, sy2;
        view_.worldToScreen(x, wy0, sx, sy);
        view_.worldToScreen(x, wy1, sx2, sy2);
        draw::line(ren_, sx, view_.viewY, sx2, view_.viewY + view_.viewH, columnColor);
    }
}

// Pick the player-supplied building sprite for the current time of day and a
// deterministic per-building "lights on" state. Returns nullptr when the art is
// missing so renderBuilding() can fall back to the procedural look.
SDL_Texture* Game::buildingTexture(const Building& b) const {
    if (!textures_.ready()) return nullptr;

    // Industry uses the dedicated factory sprite (no day/night variants).
    if (b.type == BType::Industry)
        return textures_.get("factory.bmp");

    const char* base = nullptr;
    switch (b.type) {
        case BType::Residential:   base = (b.variant & 1) ? "House" : "Apartment"; break;
        case BType::Office:        base = "Job";   break;
        case BType::PoliceStation: base = "Job";   break;   // + badge marker on top
        case BType::Hospital:      base = "House";  break;   // + cross marker on top
        default:                   return nullptr;            // Park etc.
    }

    float hour = world_.hourOfDay();
    bool day = settings().dayNight ? (hour >= 6.0f && hour < 18.0f) : true;
    // Deterministic lit state: most lights on at night, a few on by day.
    int rseed = (int)((b.windowSeed >> 9) & 255);
    bool lit  = day ? (rseed < 96) : (rseed < 184);

    std::string name = std::string(base) + (day ? "Day" : "Night")
                     + "Light" + (lit ? "ON" : "OFF") + ".png";
    return textures_.get(name);
}

void Game::renderBuilding(const Building& b, Uint8 alpha) {
    SDL_Rect r;
    if (!view_.worldRectToScreen(b.pos.x, b.pos.y, b.w, b.h, r)) return;
    float bright = dayBrightness();
    auto A = [&](SDL_Color c) { c.a = (Uint8)((int)c.a * alpha / 255); return c; };

    // ---- Sprite path: draw the player's PNG/BMP building art. -------------
    // Sprites render at NATIVE resolution: `b.w` and `b.h` are set to the
    // source PNG's actual dimensions (e.g., 48×48). The world → screen transform
    // scales them by zoom; at zoom 1.0, one world unit == one screen pixel, so
    // the art is pixel-perfect. The footprint (screen rect `r`) anchors the
    // sprite bottom-center at the base of the building.
    if (SDL_Texture* tex = buildingTexture(b)) {
        // `r` is already the screen rect of the footprint (b.pos, b.w, b.h).
        // Anchor the sprite at the footprint's bottom-center.
        int cx = r.x + r.w / 2;                        // screen x-center
        SDL_Rect dst{ cx - r.w / 2, r.y + r.h - r.h, r.w, r.h };  // bottom-anchored

        SDL_SetTextureAlphaMod(tex, alpha);          // occlusion x-ray support
        SDL_RenderCopy(ren_, tex, nullptr, &dst);
        SDL_SetTextureAlphaMod(tex, 255);

        // Keep the identifying markers for special buildings on top of the art.
        if (b.type == BType::Hospital && r.w > 18) {
            int cx = dst.x + r.w / 2, cy = dst.y + r.h / 2;
            int sz = std::max(4, r.w / 7);
            draw::fillRect(ren_, {cx - sz / 3, cy - sz, (2 * sz) / 3, 2 * sz}, A({226, 56, 56, 255}));
            draw::fillRect(ren_, {cx - sz, cy - sz / 3, 2 * sz, (2 * sz) / 3}, A({226, 56, 56, 255}));
        } else if (b.type == BType::PoliceStation && r.w > 18) {
            int cx = dst.x + r.w / 2, cy = dst.y + r.h / 3;
            int sz = std::max(3, r.w / 9);
            draw::fillRect(ren_, {cx - sz, cy - sz, 2 * sz, 2 * sz}, A({240, 218, 90, 255}));
            draw::fillRect(ren_, {cx - sz + 2, cy - sz + 2, 2 * sz - 4, 2 * sz - 4}, A({58, 96, 206, 255}));
        }
        return;
    }

    // ---- Procedural fallback (original look when art is missing). ---------
    // Ground shadow cast toward the lower-right; longer for taller buildings,
    // stronger at midday and gone at night.
    if (settings().shadows) {
        float zoom = view_.cam.zoom;
        int off = (int)clampf(b.h * 0.10f * zoom, 4.0f, 42.0f);
        SDL_Rect sh{ r.x + off, r.y + off, r.w, r.h };
        draw::fillRect(ren_, sh, {0, 0, 0, (Uint8)(shadowAlpha() * 130)});
    }

    // Bold, saturated base colors for the retro pixel look.
    SDL_Color base;
    switch (b.type) {
        case BType::Residential:   base = {78, 168, 96, 255};  break;
        case BType::Office:        base = {96, 132, 214, 255}; break;
        case BType::Industry:      base = {196, 132, 66, 255};  break;
        case BType::Park:          base = {74, 144, 74, 255};   break;
        case BType::PoliceStation: base = {58, 96, 206, 255};   break;
        case BType::Hospital:      base = {222, 226, 236, 255}; break;
        default:                   base = {130,130,130,255};    break;
    }
    base = scaleColor(base, (0.85f + 0.05f * b.variant) * bright);

    if (b.type == BType::Park) {
        // Parks are open lawns (normally drawn in renderParkDecor).
        draw::fillRect(ren_, r, A(base));
        return;
    }

    // Body + flat roof bar (a thick darker band across the top, no perspective).
    draw::fillRect(ren_, r, A(base));
    int roofH = std::max(3, r.h / 6);
    SDL_Rect roof{ r.x, r.y, r.w, roofH };
    draw::fillRect(ren_, roof, A(scaleColor(base, 0.55f)));

    // Chunky window grid: many small panes with solid lit/dark colors for apartments.
    int cols = std::max(3, std::min(6, (int)(b.w / 35.0f)));
    int rows = std::max(2, std::min(5, (int)(b.h / 35.0f)));
    SDL_Color litCol  = windowColor();
    SDL_Color darkCol = {28, 32, 42, 255};       // solid "lights off" pane
    float cw = b.w / cols, chh = b.h / rows;
    for (int gy = 1; gy < rows; ++gy) {          // skip top row under the roof bar
        for (int gx = 0; gx < cols; ++gx) {
            float wpx = b.pos.x + (gx + 0.2f) * cw;
            float wpy = b.pos.y + (gy + 0.2f) * chh;
            SDL_Rect wr;
            if (!view_.worldRectToScreen(wpx, wpy, cw * 0.5f, chh * 0.5f, wr)) continue;
            if (wr.w < 2 || wr.h < 2) continue;
            unsigned s = b.windowSeed ^ hash2i(gx, gy);
            bool darkPane = ((s >> 13) & 7) < 2;  // a few panes are dark
            draw::fillRect(ren_, wr, A(darkPane ? darkCol : litCol));
            if (!darkPane && wr.w >= 4 && wr.h >= 4)  // pixel frame on lit panes
                draw::rect(ren_, wr, A(scaleColor(litCol, 0.45f)));
        }
    }

    // Blocky type markers.
    if (b.type == BType::Hospital && r.w > 16 && r.h > 16) {
        int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        int sz = std::max(4, r.w / 6);
        draw::fillRect(ren_, {cx - sz / 3, cy - sz, (2 * sz) / 3, 2 * sz}, A({226, 56, 56, 255}));
        draw::fillRect(ren_, {cx - sz, cy - sz / 3, 2 * sz, (2 * sz) / 3}, A({226, 56, 56, 255}));
    } else if (b.type == BType::PoliceStation && r.w > 16 && r.h > 16) {
        int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        int sz = std::max(4, r.w / 6);
        draw::fillRect(ren_, {cx - sz, cy - sz, 2 * sz, 2 * sz}, A({240, 218, 90, 255}));
        draw::fillRect(ren_, {cx - sz + 2, cy - sz + 2, 2 * sz - 4, 2 * sz - 4}, A({58, 96, 206, 255}));
    } else if (b.type == BType::Industry && r.w > 16) {
        SDL_Rect ch{ r.x + (int)(r.w * 0.62f), r.y - std::max(5, r.h / 5),
                     std::max(4, r.w / 9), std::max(5, r.h / 4) };
        draw::fillRect(ren_, ch, A(scaleColor({86, 66, 52, 255}, bright)));
    }

    // Hard pixel outline.
    int ot = (r.w > 40 && r.h > 40) ? 2 : 1;
    draw::thickRect(ren_, r, ot, A(scaleColor(base, 0.4f)));
}

void Game::renderTree(const Tree& t) {
    int sx, sy;
    view_.worldToScreen(t.pos.x, t.pos.y, sx, sy);
    float zoom = view_.cam.zoom;
    int h = (int)clampf(t.height * zoom, 4.0f, 220.0f);
    if (sx < view_.viewX - 50 || sx > view_.viewX + view_.viewW + 50 ||
        sy < view_.viewY - 90 || sy > view_.viewY + view_.viewH + 50) return;
    float bright = dayBrightness();

    // Blocky ground shadow.
    if (settings().shadows && h > 8) {
        int shw = std::max(3, h / 2), shh = std::max(2, h / 6);
        draw::fillRect(ren_, { sx - shw / 2 + h / 8, sy - shh / 2, shw, shh },
                       {0, 0, 0, (Uint8)(shadowAlpha() * 120)});
    }

    // Sprite path: draw the player's tree art at native resolution (16×16 world
    // units, scaled by zoom). Base anchored at the trunk position.
    if (SDL_Texture* tex = textures_.get("tree.bmp")) {
        int side = std::max(4, (int)std::lround(16.0f * zoom));
        SDL_Rect dst{ sx - side / 2, sy - side, side, side };
        SDL_RenderCopy(ren_, tex, nullptr, &dst);
        return;
    }

    sx = (sx / 2) * 2; sy = (sy / 2) * 2;        // snap to pixel grid

    int trunkH = std::max(2, h / 3);
    int trunkW = std::max(2, h / 8);
    SDL_Color trunk = scaleColor({110, 72, 42, 255}, bright);
    draw::fillRect(ren_, { sx - trunkW / 2, sy - trunkH, trunkW, trunkH }, trunk);

    int bottom = sy - trunkH;                    // canopy sits on top of trunk
    unsigned s = t.seed;
    auto jitter = [&](int range) { s = s * 1103515245u + 12345u; return (int)((s >> 16) % (2 * range + 1)) - range; };

    switch (t.type) {
        case TreeType::Deciduous: {
            // Three stacked squares forming a chunky leafy blob.
            SDL_Color leaf  = scaleColor({58, 150, 68, 255}, bright);
            SDL_Color leaf2 = scaleColor({84, 184, 92, 255}, bright);
            int cs = std::max(3, h / 3);
            draw::fillRect(ren_, { sx - cs,       bottom - cs,         cs * 2,     cs }, leaf);
            draw::fillRect(ren_, { sx - cs * 3/4, bottom - cs * 2 + 1, cs * 3 / 2, cs }, leaf2);
            draw::fillRect(ren_, { sx - cs / 2,   bottom - cs * 3 + 2, cs,         cs }, leaf);
            draw::fillRect(ren_, { sx - cs / 2,   bottom - cs * 3 + 2,
                                   std::max(2, cs / 2), std::max(2, cs / 2) },
                           scaleColor(leaf2, 1.1f));   // top-left highlight block
            break;
        }
        case TreeType::Pine: {
            // Stepped pyramid of rectangles (angular and blocky).
            SDL_Color pc  = scaleColor({40, 120, 66, 255}, bright);
            SDL_Color pc2 = scaleColor({54, 142, 80, 255}, bright);
            int tiers = 4;
            int tierH = std::max(2, h / tiers);
            int wMax  = std::max(3, h / 2);
            for (int i = 0; i < tiers; ++i) {
                int tw = std::max(2, wMax - (wMax * i) / tiers);
                int ty = bottom - (i + 1) * tierH;
                draw::fillRect(ren_, { sx - tw / 2, ty, tw, tierH + 1 }, (i & 1) ? pc2 : pc);
            }
            break;
        }
        case TreeType::Willow: {
            // Top canopy block with hanging vertical strands.
            SDL_Color wc  = scaleColor({92, 160, 86, 255}, bright);
            SDL_Color wcd = scaleColor({70, 128, 70, 255}, bright);
            int cs = std::max(3, h / 3);
            draw::fillRect(ren_, { sx - cs, bottom - cs * 2, cs * 2, cs }, wc);
            for (int i = -2; i <= 2; ++i) {
                int dx  = i * std::max(2, cs / 2);
                int len = cs + (i == 0 ? cs / 2 : (std::abs(i) == 1 ? cs / 3 : 0));
                draw::fillRect(ren_, { sx + dx - std::max(1, cs / 6), bottom - cs,
                                       std::max(1, cs / 3), len }, wcd);
            }
            break;
        }
        case TreeType::Dead:
        default: {
            // Bare branches as simple line segments.
            SDL_Color c = scaleColor({128, 100, 80, 255}, bright);
            int topY = bottom - std::max(3, h / 2);
            draw::thickLine(ren_, sx, bottom, sx, topY, std::max(1, trunkW / 2), c);
            int branches = 4;
            for (int i = 0; i < branches; ++i) {
                int by = topY + ((bottom - topY) * i) / branches;
                int len = std::max(3, (h / 2) - i * 2);
                draw::line(ren_, sx, by, sx - len / 2 + jitter(2), by - len / 2, c);
                draw::line(ren_, sx, by, sx + len / 2 + jitter(2), by - len / 2, c);
            }
            break;
        }
    }
}

void Game::renderAgent(const Agent& a) {
    if (!a.alive || a.sleeping) return;
    int sx, sy;
    view_.worldToScreen(a.pos.x, a.pos.y, sx, sy);
    if (sx < view_.viewX - 8 || sx > view_.viewX + view_.viewW + 8 ||
        sy < view_.viewY - 8 || sy > view_.viewY + view_.viewH + 8) return;

    float zoom = view_.cam.zoom;
    int rad = (int)clampf(zoom * 4.5f, 2.0f, 16.0f);
    SDL_Color col = roleColor(a.role);
    sx = (sx / 2) * 2; sy = (sy / 2) * 2;        // snap to 2px grid

    // Far zoom: cheap blocky dot.
    if (rad <= 2) {
        SDL_Rect p{ sx - 1, sy - 1, 3, 3 };
        draw::fillRect(ren_, p, col);
        if (a.id == selectedId_) draw::thickRect(ren_, {sx - 3, sy - 3, 6, 6}, 1, {255, 255, 0, 255});
        return;
    }

    // Figure metrics (a little blocky person).
    int bw = std::max(3, rad);                   // body width
    int bh = std::max(3, (int)(rad * 1.1f));     // body height
    int hs = std::max(2, rad / 2);               // head square
    int legH = std::max(1, rad / 3);
    int totalH = hs + bh + legH;
    int top = sy - totalH / 2;
    int legY = top + hs + bh;

    // Two-frame leg shuffle instead of a smooth bob.
    bool walking = settings().animations && (a.act == Act::Walk || a.act == Act::Flee);
    int frame = walking ? (((int)(a.animPhase * 3.0f)) & 1) : -1;

    // Blocky ground shadow.
    if (settings().shadows && rad >= 3) {
        int shw = std::max(3, bw), shh = std::max(1, rad / 3);
        draw::fillRect(ren_, { sx - shw / 2 + 1, sy + totalH / 2 - shh, shw, shh },
                       {0, 0, 0, (Uint8)(shadowAlpha() * 90)});
    }

    // Selection bracket (square, pulsing).
    if (a.id == selectedId_) {
        int p = (std::sin(menuAnim_ * 6.0f) > 0) ? 1 : 0;
        draw::thickRect(ren_, { sx - bw / 2 - 3 - p, top - 3 - p,
                                bw + 6 + p * 2, totalH + 6 + p * 2 }, 2, {255, 255, 0, 255});
    }

    // Action flash: blocky expanding square.
    if (a.actFlash > 0.0f) {
        SDL_Color fc;
        switch (a.act) {
            case Act::Rob:    fc = {255, 210, 40, 255};  break;
            case Act::Arrest: fc = {64, 144, 255, 255};  break;
            case Act::Heal:   fc = {66, 220, 120, 255};  break;
            case Act::Fight:  fc = {255, 110, 40, 255};  break;
            default:          fc = {255, 255, 255, 255}; break;
        }
        fc.a = (Uint8)(a.actFlash * 200);
        int g = (int)(rad * (1.0f + (1.0f - a.actFlash) * 1.6f));
        draw::thickRect(ren_, { sx - g, sy - g, g * 2, g * 2 }, 1, fc);
    }

    // Legs (alternate which foot steps forward/down).
    SDL_Color legc = scaleColor(col, 0.55f);
    int legW = std::max(1, bw / 3);
    int lA = legH, lB = legH;
    if (frame == 0) lA = legH + 1; else if (frame == 1) lB = legH + 1;
    draw::fillRect(ren_, { sx - bw / 2,        legY, legW, lA }, legc);
    draw::fillRect(ren_, { sx + bw / 2 - legW, legY, legW, lB }, legc);

    // Body.
    SDL_Rect body{ sx - bw / 2, top + hs, bw, bh };
    draw::fillRect(ren_, body, col);
    draw::rect(ren_, body, scaleColor(col, 0.5f));

    // Head (lighter, shifted slightly by facing).
    int hx = sx - hs / 2 + (a.facing > 0 ? 1 : -1);
    SDL_Rect head{ hx, top, hs, hs };
    draw::fillRect(ren_, head, scaleColor(col, 1.25f));
    draw::rect(ren_, head, scaleColor(col, 0.55f));

    // Stress indicator bar above the head when zoomed in.
    if (rad >= 6 && a.stress > 0.5f) {
        SDL_Rect bar{ sx - bw / 2, top - 4, (int)(bw * a.stress), 2 };
        draw::fillRect(ren_, bar, {240, 80, 60, 220});
    }
}

// Sync building world sizes to their sprite native dimensions. After world
// generation, iterate each building and set w/h from the texture's actual
// pixel width/height via SDL_QueryTexture(). This makes every building's
// footprint, Y-sort key, and rendered size all use the true per-file native
// dimensions. Parks stay lawn-sized; other buildings adopt their sprite size.
void Game::syncSpriteSizes() {
    for (auto& b : world_.buildings) {
        if (b.type == BType::Park) continue;  // parks are open lawns, keep small
        SDL_Texture* tex = buildingTexture(b);
        if (tex) {
            int w = 0, h = 0;
            SDL_QueryTexture(tex, nullptr, nullptr, &w, &h);
            if (w > 0 && h > 0) {
                b.w = (float)w;
                b.h = (float)h;
            }
        } else {
            // Fallback: use default native size (48×48 for all building types)
            b.w = 48.0f;
            b.h = 48.0f;
        }
    }
}

// Cosmetic background traffic. The car sprites are square with the vehicle
// drawn facing right, so we render a square world rect (no stretching) centered
// on the car and rotate it to face the direction of travel.
void Game::renderVehicle(const Vehicle& v) {
    const char* name = nullptr;
    float worldSize = 26.0f;
    switch (v.type) {
        case VehicleType::Civilian: name = "CivilianCar.png"; worldSize = 26.0f; break;
        case VehicleType::Luxury:   name = "LuxuryCar.png";   worldSize = 26.0f; break;
        case VehicleType::Truck:    name = "Truck.png";       worldSize = 40.0f; break;
        default: return;
    }
    SDL_Texture* tex = textures_.get(name);
    if (!tex) return;

    SDL_Rect dst;
    if (!view_.worldRectToScreen(v.pos.x - worldSize * 0.5f,
                                 v.pos.y - worldSize * 0.5f,
                                 worldSize, worldSize, dst)) return;
    if (dst.w <= 0 || dst.h <= 0) return;

    double ang = v.horizontal ? (v.dir >= 0 ? 0.0  : 180.0)
                              : (v.dir >= 0 ? 90.0 : -90.0);
    SDL_RenderCopyEx(ren_, tex, nullptr, &dst, ang, nullptr, SDL_FLIP_NONE);
}

void Game::renderParticles() {
    SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
    for (const auto& p : world_.particles) {
        int sx, sy;
        view_.worldToScreen(p.pos.x, p.pos.y, sx, sy);
        float t = clampf(p.life / p.maxLife, 0.0f, 1.0f);
        SDL_Color c = p.color;
        c.a = (Uint8)(c.a * t);
        int s = std::max(2, (int)(p.size * view_.cam.zoom));  // chunky pixel blocks
        sx = (sx / 2) * 2; sy = (sy / 2) * 2;
        SDL_Rect rc{ sx - s / 2, sy - s / 2, s, s };
        draw::fillRect(ren_, rc, c);
    }
}

void Game::renderFloats() {
    for (const auto& f : world_.floats) {
        int sx, sy;
        view_.worldToScreen(f.pos.x, f.pos.y, sx, sy);
        float t = clampf(f.life / f.maxLife, 0.0f, 1.0f);
        SDL_Color c = f.color; c.a = (Uint8)(255 * t);
        font::drawShadowed(ren_, f.text, sx, sy, 1, c, Align::Center);
    }
}

void Game::renderDayNight() {
    if (!settings().dayNight) return;
    float b = dayBrightness();
    float night = clampf(1.0f - b, 0.0f, 1.0f);
    if (night <= 0.01f) return;
    SDL_Rect v{ view_.viewX, view_.viewY, view_.viewW, view_.viewH };
    SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren_, 6, 10, 34, (Uint8)(night * 150));
    SDL_RenderFillRect(ren_, &v);
}

// =====================================================================
// HUD (top bar)
// =====================================================================
void Game::renderHUD() {
    auto& s = settings();
    SDL_Rect bar{ 0, 0, s.screenW, HUD_H };
    draw::fillRect(ren_, bar, {16, 20, 30, 245});
    draw::line(ren_, 0, HUD_H, s.screenW, HUD_H, {60, 80, 120, 255});

    font::drawShadowed(ren_, "OMNIVERSE", 14, 8, 3, ui::accent());
    font::draw(ren_, mode_ == GameMode::Sandbox ? "SANDBOX MODE" : "LOGOS ENGINE", 14, 32, 1,
               mode_ == GameMode::Sandbox ? SDL_Color{120, 220, 160, 255} : ui::textDim());

    // Version number in top-right corner
    font::draw(ren_, "V.1.11 OmniVerse", s.screenW - 12, 8, 1, ui::textDim(), Align::Right);

    // Role chips (compact so they never collide with the right-side readout).
    int x = 226;
    const char* labels[] = {"CIV", "CRIM", "POL", "GANG", "HEAL"};
    for (int i = 0; i < (int)Role::COUNT; ++i) {
        SDL_Color c = roleColor((Role)i);
        draw::fillRect(ren_, {x, HUD_H / 2 - 5, 10, 10}, c);
        draw::rect(ren_, {x, HUD_H / 2 - 5, 10, 10}, scaleColor(c, 0.5f));
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%s %d", labels[i], world_.stats.count[i]);
        font::draw(ren_, buf, x + 14, HUD_H / 2 - 3, 1, ui::textMain());
        x += 14 + font::textWidth(buf, 1) + 12;
    }

    // Right side: clock, day phase, fps, speed, entity count.
    int visibleCount = countVisibleEntities();
    const char* phase = "DAY";
    if (s.dayNight) {
        float h = world_.hourOfDay();
        phase = (h >= 22.0f || h < 5.0f) ? "NIGHT"
              : (h < 8.0f)  ? "DAWN"
              : (h < 17.0f) ? "DAY"
              : (h < 20.0f) ? "DUSK" : "NIGHT";
    }
    char rbuf[96];
    if (s.dayNight) {
        float hf = world_.hourOfDay();
        int hh = (int)hf, mm = (int)((hf - hh) * 60.0f) % 60;
        std::snprintf(rbuf, sizeof(rbuf), "%02d:%02d %s   FPS %d   SPEED %.1fX",
                      hh, mm, phase, (int)fps_, simSpeed_);
    } else {
        std::snprintf(rbuf, sizeof(rbuf), "%s   FPS %d   SPEED %.1fX", phase, (int)fps_, simSpeed_);
    }
    int rw = font::textWidth(rbuf, 2);
    font::draw(ren_, rbuf, s.screenW - 12, HUD_H / 2 - 6, 2, ui::textDim(), Align::Right);
    // On-screen entity count in accent so it stands out.
    char ebuf[32];
    std::snprintf(ebuf, sizeof(ebuf), "ON-SCREEN %d", visibleCount);
    font::draw(ren_, ebuf, s.screenW - 12 - rw - 18, HUD_H / 2 - 6, 2, ui::accent(), Align::Right);
}

// =====================================================================
// Sidebar (right)
// =====================================================================
void Game::renderSidebar() {
    auto& s = settings();
    SDL_Rect side{ s.screenW - SIDE_W, HUD_H, SIDE_W, s.screenH - HUD_H };
    draw::fillRect(ren_, side, {14, 18, 28, 250});
    draw::line(ren_, side.x, side.y, side.x, side.y + side.h, {60, 80, 120, 255});

    int pad = 14;
    int x = side.x + pad;
    int w = SIDE_W - pad * 2;
    int y = side.y + pad;
    bool sandbox = (mode_ == GameMode::Sandbox);

    // --- Header / meters ---
    if (sandbox) {
        font::draw(ren_, "SANDBOX", x, y, 2, {120, 220, 160, 255}); y += 22;
        font::draw(ren_, "FREE BUILD - NO TIMER", x, y, 1, ui::textDim()); y += 18;
        font::draw(ren_, "BUDGET: UNLIMITED", x, y, 2, {255, 215, 90, 255}); y += 24;
    } else {
        font::draw(ren_, "OBJECTIVE", x, y, 2, ui::accent()); y += 22;
        int remain = std::max(0, (int)(goalTime_ - gameTimer_));
        char ob[64]; std::snprintf(ob, sizeof(ob), "SURVIVE %02d:%02d", remain / 60, remain % 60);
        font::draw(ren_, ob, x, y, 2, ui::textMain()); y += 22;

        font::draw(ren_, "CITY SAFETY", x, y, 1, ui::textDim()); y += 12;
        SDL_Rect sm{ x, y, w, 14 };
        draw::roundedRect(ren_, sm, 4, {40, 46, 60, 255});
        SDL_Color safeCol = lerpColor({230, 70, 60, 255}, {70, 210, 110, 255}, safety_ / 100.0f);
        draw::roundedRect(ren_, { x, y, (int)(w * safety_ / 100.0f), 14 }, 4, safeCol);
        y += 22;

        char bb[48]; std::snprintf(bb, sizeof(bb), "BUDGET: %d / 500", (int)budget_);
        font::draw(ren_, bb, x, y, 2, {255, 215, 90, 255}); y += 20;
        int crime = world_.stats.count[(int)Role::Criminal] + world_.stats.count[(int)Role::Gang];
        char sl[64]; std::snprintf(sl, sizeof(sl), "SCORE %d   BOUNTIES %d", liveScore(), crime);
        font::draw(ren_, sl, x, y, 1, ui::textDim()); y += 18;
    }

    // --- Deploy / spawn tools ---
    if (sandbox) {
        font::draw(ren_, "SPAWN (CLICK MAP)", x, y, 1, ui::textDim()); y += 14;
        struct SB { const char* label; Tool tool; Role role; };
        SB row1[] = { {"CIVIL 3", Tool::SpawnCivil, Role::Civil},
                      {"CRIME 4", Tool::SpawnCriminal, Role::Criminal},
                      {"GANG 7",  Tool::SpawnGang, Role::Gang} };
        int bw3 = (w - 8) / 3;
        for (int i = 0; i < 3; ++i) {
            SDL_Rect b{ x + i * (bw3 + 4), y, bw3, 28 };
            SDL_Color ac = (tool_ == row1[i].tool) ? SDL_Color{255, 255, 160, 255} : roleColor(row1[i].role);
            if (ui::button(ren_, b, row1[i].label, in_, ac, 1)) {
                tool_ = (tool_ == row1[i].tool) ? Tool::None : row1[i].tool; audio_.click();
            }
        }
        y += 32;
        SB row2[] = { {"POLICE 5", Tool::SpawnPolice, Role::Police},
                      {"HEAL 6",   Tool::SpawnHealer, Role::Healer} };
        int bw2 = (w - 4) / 2;
        for (int i = 0; i < 2; ++i) {
            SDL_Rect b{ x + i * (bw2 + 4), y, bw2, 28 };
            SDL_Color ac = (tool_ == row2[i].tool) ? SDL_Color{255, 255, 160, 255} : roleColor(row2[i].role);
            if (ui::button(ren_, b, row2[i].label, in_, ac, 1)) {
                tool_ = (tool_ == row2[i].tool) ? Tool::None : row2[i].tool; audio_.click();
            }
        }
        y += 34;
        SDL_Rect bgrid{ x, y, bw2, 26 };
        SDL_Rect bresp{ x + bw2 + 4, y, bw2, 26 };
        bool gridOn = showGrid_;
        if (ui::toggle(ren_, bgrid, "GRID G", gridOn, in_, 1)) { showGrid_ = gridOn; audio_.click(); }
        if (ui::button(ren_, bresp, "RESPAWN N", in_, {120, 160, 220, 255}, 1)) {
            world_.regenerateAgentsOnly(); selectedId_ = -1; audio_.click(); toast("Population respawned");
        }
        y += 30;
    } else {
        font::draw(ren_, "DEPLOY (CLICK MAP)", x, y, 1, ui::textDim()); y += 14;
        SDL_Rect bp{ x, y, w / 2 - 4, 30 };
        SDL_Rect bh{ x + w / 2 + 4, y, w / 2 - 4, 30 };
        SDL_Color polCol = (tool_ == Tool::Police) ? SDL_Color{120, 180, 255, 255} : roleColor(Role::Police);
        SDL_Color heaCol = (tool_ == Tool::Healer) ? SDL_Color{120, 240, 170, 255} : roleColor(Role::Healer);
        if (ui::button(ren_, bp, "POLICE 100", in_, polCol, 1)) {
            tool_ = (tool_ == Tool::Police) ? Tool::None : Tool::Police; audio_.click();
        }
        if (ui::button(ren_, bh, "HEALER 80", in_, heaCol, 1)) {
            tool_ = (tool_ == Tool::Healer) ? Tool::None : Tool::Healer; audio_.click();
        }
        y += 36;
    }

    // --- Build tools (place buildings; free in sandbox, costs budget in survival) ---
    font::draw(ren_, sandbox ? "BUILD (CLICK MAP)" : "BUILD (COSTS BUDGET)", x, y, 1, ui::textDim());
    y += 14;
    struct BT { const char* label; Tool tool; SDL_Color col; };
    BT brow[] = {
        {"HOUSE",  Tool::BuildResidential, {150, 195, 140, 255}},
        {"OFFICE", Tool::BuildOffice,      {130, 170, 215, 255}},
        {"FACTORY",Tool::BuildIndustry,    {205, 160, 120, 255}},
        {"PARK",   Tool::BuildPark,        {110, 205, 130, 255}},
    };
    int bw4 = (w - 12) / 4;
    for (int i = 0; i < 4; ++i) {
        SDL_Rect bb{ x + i * (bw4 + 4), y, bw4, 28 };
        SDL_Color ac = (tool_ == brow[i].tool) ? SDL_Color{255, 255, 160, 255} : brow[i].col;
        if (ui::button(ren_, bb, brow[i].label, in_, ac, 1)) {
            tool_ = (tool_ == brow[i].tool) ? Tool::None : brow[i].tool; audio_.click();
        }
    }
    y += 32;

    if (tool_ != Tool::None) {
        font::draw(ren_, "TAB = CANCEL TOOL", x, y, 1, ui::accent());
    }
    y += 16;

    // --- Sandbox edit controls (undo/redo/save/load) ---
    if (sandbox) {
        font::draw(ren_, "EDIT TOOLS", x, y, 1, ui::textDim());
        y += 14;
        int bw2 = (w - 8) / 2;
        SDL_Rect undo{ x, y, bw2, 24 };
        SDL_Rect redo{ x + bw2 + 4, y, bw2, 24 };
        if (ui::button(ren_, undo, "UNDO (Z)", in_, {200, 150, 100, 255}, 1)) { undoLastAction(); }
        if (ui::button(ren_, redo, "REDO (Y)", in_, {200, 150, 100, 255}, 1)) { redoLastAction(); }
        y += 28;
        SDL_Rect save{ x, y, bw2, 24 };
        SDL_Rect load{ x + bw2 + 4, y, bw2, 24 };
        std::string savePath = "layout.json";
#ifdef __ANDROID__
        char* pref = SDL_GetPrefPath("OmniVerse", "OmniVerse");
        if (pref) { savePath = std::string(pref) + "layout.json"; SDL_free(pref); }
#endif
        if (ui::button(ren_, save, "SAVE (S)", in_, {100, 200, 150, 255}, 1)) { saveLayout(savePath); }
        if (ui::button(ren_, load, "LOAD (L)", in_, {100, 200, 150, 255}, 1)) { loadLayout(savePath); }
        y += 28;
    }

    // --- Speed controls ---
    font::draw(ren_, "SIM SPEED", x, y, 1, ui::textDim()); y += 14;
    const char* sp[] = {"0.5X", "1X", "2X", "4X"};
    int bw = (w - 12) / 4;
    for (int i = 0; i < 4; ++i) {
        SDL_Rect sb{ x + i * (bw + 4), y, bw, 28 };
        SDL_Color ac = (i == speedIndex_) ? ui::accent() : SDL_Color{70, 86, 120, 255};
        if (ui::button(ren_, sb, sp[i], in_, ac, 1)) { setSpeed(i); audio_.click(); }
    }
    y += 36;

    // --- Selected agent info / editor ---
    int infoH = sandbox ? 162 : 96;
    SDL_Rect infoArea{ x, y, w, infoH };
    renderInfoPanel(infoArea);
    y += infoH + 8;

    // --- Minimap ---
    int mmH = std::min(w, side.y + side.h - y - 120);
    if (mmH > 60) {
        SDL_Rect mm{ x, y, w, mmH };
        renderMinimap(mm);
        y += mmH + 10;
    }

    // --- Event log fills the rest ---
    SDL_Rect logArea{ x, y, w, side.y + side.h - y - pad };
    if (logArea.h > 40) renderEventLog(logArea);
}

void Game::renderInfoPanel(const SDL_Rect& area) {
    ui::panel(ren_, area);
    int x = area.x + 10, y = area.y + 8;
    bool sandbox = (mode_ == GameMode::Sandbox);
    if (selectedId_ < 0 || selectedId_ >= (int)world_.agents.size()) {
        font::draw(ren_, "NO SELECTION", x, y, 2, ui::textDim());
        font::draw(ren_, sandbox ? "CLICK AGENT TO EDIT" : "CLICK AN AGENT", x, y + 20, 1, ui::textDim());
        return;
    }
    Agent& a = world_.agents[selectedId_];
    SDL_Color rc = roleColor(a.role);
    draw::fillRect(ren_, {x, y, 16, 16}, a.alive ? rc : SDL_Color{90, 90, 90, 255});
    draw::rect(ren_, {x, y, 16, 16}, scaleColor(a.alive ? rc : SDL_Color{90, 90, 90, 255}, 0.5f));
    char hdr[48]; std::snprintf(hdr, sizeof(hdr), "%s #%d", roleName(a.role), a.id);
    font::draw(ren_, hdr, x + 24, y + 2, 2, ui::textMain());
    if (!a.alive) { font::draw(ren_, "STATUS: DECEASED", x, y + 24, 1, {220, 90, 90, 255}); return; }

    // Sandbox: live-editable stat sliders + remove button.
    if (sandbox) {
        int sw = area.w - 20;
        int yy = y + 30;
        ui::sliderF(ren_, {x, yy, sw, 16}, "STRESS", a.stress, 0.0f, 1.0f, in_); yy += 34;
        ui::sliderF(ren_, {x, yy, sw, 16}, "HEALTH", a.health, 0.0f, 1.0f, in_); yy += 34;
        ui::sliderF(ren_, {x, yy, sw, 16}, "MONEY",  a.money,  0.0f, 500.0f, in_); yy += 28;
        SDL_Rect rem{ x, yy, sw, 22 };
        if (ui::button(ren_, rem, "REMOVE AGENT", in_, {200, 90, 90, 255}, 1)) {
            a.alive = false; audio_.click();
            world_.spawnBurst(a.pos, {150, 150, 160, 255}, 8, 60.0f);
            selectedId_ = -1;
        }
        return;
    }

    char mb[32]; std::snprintf(mb, sizeof(mb), "MONEY: %d C", (int)a.money);
    font::draw(ren_, mb, x, y + 24, 1, {255, 215, 90, 255});

    // Stress bar.
    font::draw(ren_, "STRESS", x, y + 38, 1, ui::textDim());
    SDL_Rect sb{ x + 50, y + 38, area.w - 70, 8 };
    draw::roundedRect(ren_, sb, 3, {40, 46, 60, 255});
    draw::roundedRect(ren_, { sb.x, sb.y, (int)(sb.w * a.stress), 8 }, 3,
                      lerpColor({90, 200, 120, 255}, {235, 80, 60, 255}, a.stress));
    // Health bar.
    font::draw(ren_, "HEALTH", x, y + 52, 1, ui::textDim());
    SDL_Rect hb{ x + 50, y + 52, area.w - 70, 8 };
    draw::roundedRect(ren_, hb, 3, {40, 46, 60, 255});
    draw::roundedRect(ren_, { hb.x, hb.y, (int)(hb.w * a.health), 8 }, 3, {90, 200, 120, 255});

    const char* act = "IDLE";
    switch (a.act) {
        case Act::Walk: act = "WALKING"; break;
        case Act::Rob:  act = "ROBBING"; break;
        case Act::Heal: act = "HEALING"; break;
        case Act::Arrest: act = "ARRESTING"; break;
        case Act::Flee: act = "FLEEING"; break;
        case Act::Fight: act = "FIGHTING"; break;
        default: break;
    }
    font::draw(ren_, std::string("ACTION: ") + act, x, y + 66, 1, ui::textMain());
}

void Game::renderMinimap(const SDL_Rect& area) {
    ui::panel(ren_, area);
    auto& s = settings();
    int pad = 6;
    SDL_Rect inner{ area.x + pad, area.y + pad, area.w - 2 * pad, area.h - 2 * pad };
    draw::fillRect(ren_, inner, {10, 14, 20, 255});

    float scaleX = inner.w / s.worldW;
    float scaleY = inner.h / s.worldH;

    for (const auto& b : world_.buildings) {
        SDL_Rect r{ inner.x + (int)(b.pos.x * scaleX), inner.y + (int)(b.pos.y * scaleY),
                    std::max(1, (int)(b.w * scaleX)), std::max(1, (int)(b.h * scaleY)) };
        SDL_Color c = b.type == BType::Park ? SDL_Color{40, 90, 50, 255} : SDL_Color{60, 70, 88, 255};
        draw::fillRect(ren_, r, c);
    }
    // Agents as dots (sample to keep it cheap).
    int step = std::max(1, (int)world_.agents.size() / 600);
    for (int i = 0; i < (int)world_.agents.size(); i += step) {
        const Agent& a = world_.agents[i];
        if (!a.alive) continue;
        int px = inner.x + (int)(a.pos.x * scaleX);
        int py = inner.y + (int)(a.pos.y * scaleY);
        SDL_SetRenderDrawColor(ren_, roleColor(a.role).r, roleColor(a.role).g, roleColor(a.role).b, 255);
        SDL_RenderDrawPoint(ren_, px, py);
    }
    // Camera viewport box.
    float vw0, vh0, vw1, vh1;
    view_.screenToWorld(view_.viewX, view_.viewY, vw0, vh0);
    view_.screenToWorld(view_.viewX + view_.viewW, view_.viewY + view_.viewH, vw1, vh1);
    SDL_Rect cam{ inner.x + (int)(vw0 * scaleX), inner.y + (int)(vh0 * scaleY),
                  (int)((vw1 - vw0) * scaleX), (int)((vh1 - vh0) * scaleY) };
    draw::rect(ren_, cam, {255, 255, 120, 220});
    font::draw(ren_, "MINIMAP", area.x + 8, area.y + 4, 1, ui::textDim());
}

void Game::renderEventLog(const SDL_Rect& area) {
    ui::panel(ren_, area);
    font::draw(ren_, "EVENT LOG", area.x + 8, area.y + 6, 1, ui::accent());
    int lineH = font::textHeight(1) + 4;
    int maxLines = (area.h - 22) / lineH;
    int n = (int)world_.log.size();
    int start = std::max(0, n - maxLines);
    int y = area.y + 20;
    for (int i = start; i < n; ++i) {
        const LogEntry& e = world_.log[i];
        std::string line = e.text;
        // Truncate to fit width.
        int maxChars = (area.w - 16) / ((font::GLYPH_W + 1));
        if ((int)line.size() > maxChars) line = line.substr(0, std::max(0, maxChars - 1));
        font::draw(ren_, line, area.x + 8, y, 1, e.color);
        y += lineH;
    }
}

// =====================================================================
// Hints / toast
// =====================================================================
void Game::renderHints() {
    const char* h = (mode_ == GameMode::Sandbox)
        ? "WASD/DRAG PAN  WHEEL ZOOM  3-7 SPAWN  1-2 DEPLOY  G GRID  N RESPAWN  TAB CANCEL  ESC BACK"
        : "WASD/DRAG PAN   WHEEL ZOOM   1 POLICE  2 HEALER   SPACE PAUSE   H HELP   R RESET";
    int y = settings().screenH - 20;
    SDL_Rect bg{ 0, y - 4, view_.viewX + view_.viewW, 24 };
    SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
    draw::fillRect(ren_, bg, {10, 14, 22, 180});
    font::draw(ren_, h, 10, y, 1, ui::textDim());
}

void Game::renderToast() {
    if (toast_ <= 0) return;
    float a = clampf(toast_, 0.0f, 1.0f);
    int tw = font::textWidth(toastMsg_, 2);
    SDL_Rect box{ settings().screenW / 2 - tw / 2 - 16, 60, tw + 32, 32 };
    SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
    SDL_Color bg = ui::panelBg(); bg.a = (Uint8)(bg.a * a);
    draw::roundedRect(ren_, box, 8, bg);
    draw::roundedRectOutline(ren_, box, 8, ui::accent());
    SDL_Color tc = ui::textMain(); tc.a = (Uint8)(255 * a);
    font::draw(ren_, toastMsg_, settings().screenW / 2, 68, 2, tc, Align::Center);
}

// =====================================================================
// Overlay screens
// =====================================================================
void Game::dimScreen(Uint8 alpha) {
    SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren_, 0, 0, 0, alpha);
    SDL_Rect full{ 0, 0, settings().screenW, settings().screenH };
    SDL_RenderFillRect(ren_, &full);
}

void Game::renderMenu() {
    auto& s = settings();
    // Animated background gradient + drifting motes.
    SDL_Rect full{ 0, 0, s.screenW, s.screenH };
    draw::vGradient(ren_, full, {22, 28, 50, 255}, {8, 10, 20, 255});
    for (int i = 0; i < 70; ++i) {
        float t = menuAnim_ * 0.2f + i * 0.37f;
        int px = (int)(std::fmod(i * 197 + menuAnim_ * (20 + i % 30), (float)s.screenW));
        int py = (int)(std::fmod(i * 113 + std::sin(t) * 40 + 100, (float)s.screenH));
        Uint8 al = (Uint8)(80 + 80 * std::sin(t * 1.7f));
        draw::fillCircle(ren_, px, py, 1 + (i % 2), {120, 160, 220, al});
    }

    int cx = s.screenW / 2;
    float bob = std::sin(menuAnim_ * 1.5f) * 4.0f;
    font::drawShadowed(ren_, "OMNIVERSE", cx, (int)(s.screenH * 0.18f + bob), 10, ui::accent(), Align::Center);
    font::draw(ren_, "LOGOS ENGINE  -  SDL2 EDITION", cx, (int)(s.screenH * 0.18f) + 90, 2, ui::textDim(), Align::Center);

    // Version number in top-right corner
    font::draw(ren_, "V.1.11 OmniVerse", s.screenW - 12, 12, 1, ui::textDim(), Align::Right);

    // Buttons.
    int bw = 280, bh = 46, gap = 14;
    int by = (int)(s.screenH * 0.42f);
    SDL_Rect bNew { cx - bw / 2, by + 0 * (bh + gap), bw, bh };
    SDL_Rect bSand{ cx - bw / 2, by + 1 * (bh + gap), bw, bh };
    SDL_Rect bSet { cx - bw / 2, by + 2 * (bh + gap), bw, bh };
    SDL_Rect bHelp{ cx - bw / 2, by + 3 * (bh + gap), bw, bh };
    SDL_Rect bQuit{ cx - bw / 2, by + 4 * (bh + gap), bw, bh };

    if (ui::button(ren_, bNew, "NEW GAME", in_, ui::accent(), 3))      { audio_.click(); startNewGame(); }
    if (ui::button(ren_, bSand, "SANDBOX MODE", in_, {120, 220, 160, 255}, 3)) { audio_.click(); startSandbox(); }
    if (ui::button(ren_, bSet, "SETTINGS", in_, {120, 140, 200, 255}, 3)) { audio_.click(); prevState_ = GState::Menu; state_ = GState::Settings; }
    if (ui::button(ren_, bHelp, "HOW TO PLAY", in_, {120, 140, 200, 255}, 3)) { audio_.click(); prevState_ = GState::Menu; state_ = GState::Help; }
    if (ui::button(ren_, bQuit, "QUIT", in_, {180, 90, 90, 255}, 3))   { audio_.click(); running_ = false; }

    font::draw(ren_, "A LIVING-CITY SIMULATION SANDBOX", cx, s.screenH - 40, 1, ui::textDim(), Align::Center);
}

void Game::renderPause() {
    dimScreen(150);
    int cx = settings().screenW / 2, cy = settings().screenH / 2;
    font::drawShadowed(ren_, "PAUSED", cx, cy - 130, 8, ui::textMain(), Align::Center);
    if (mode_ == GameMode::Sandbox) {
        font::draw(ren_, "SANDBOX MODE", cx, cy - 78, 2, {120, 220, 160, 255}, Align::Center);
    } else {
        char sc[80]; std::snprintf(sc, sizeof(sc), "PROJECTED SCORE: %d", liveScore() + (int)(safety_ * 3));
        font::draw(ren_, sc, cx, cy - 78, 2, ui::textDim(), Align::Center);
    }

    int bw = 280, bh = 46, gap = 14;
    SDL_Rect bResume{ cx - bw / 2, cy - 30, bw, bh };
    SDL_Rect bSet   { cx - bw / 2, cy - 30 + (bh + gap), bw, bh };
    SDL_Rect bMenu  { cx - bw / 2, cy - 30 + 2 * (bh + gap), bw, bh };
    if (ui::button(ren_, bResume, "RESUME", in_, ui::accent(), 3))        { audio_.click(); state_ = GState::Playing; }
    if (ui::button(ren_, bSet, "SETTINGS", in_, {120, 140, 200, 255}, 3)) { audio_.click(); prevState_ = GState::Paused; state_ = GState::Settings; }
    if (ui::button(ren_, bMenu, "MAIN MENU", in_, {180, 90, 90, 255}, 3)) { audio_.click(); state_ = GState::Menu; fade_ = 0.6f; }
}

void Game::renderSettingsScreen() {
    dimScreen(180);
    auto& s = settings();
    int pw = 680, ph = 540;
    SDL_Rect box{ s.screenW / 2 - pw / 2, s.screenH / 2 - ph / 2, pw, ph };
    ui::panel(ren_, box);
    font::draw(ren_, "SETTINGS", box.x + pw / 2, box.y + 18, 4, ui::accent(), Align::Center);

    int cy0 = box.y + 62;
    int colW = (pw - 72) / 2;                 // 24 margins + 24 gutter
    int lx = box.x + 24;
    int rx = box.x + 24 + colW + 24;

    // ---------------- Left column: quality toggles + volume ----------------
    int ly = cy0, th = 30, step = 38;
    bool b;
    b = s.animations; if (ui::toggle(ren_, {lx, ly, colW, th}, "ANIMATIONS", b, in_)) { s.animations = b; audio_.click(); } ly += step;
    b = s.shadows;    if (ui::toggle(ren_, {lx, ly, colW, th}, "SHADOWS", b, in_))    { s.shadows = b; audio_.click(); } ly += step;
    b = s.dayNight;   if (ui::toggle(ren_, {lx, ly, colW, th}, "DAY/NIGHT", b, in_))  { s.dayNight = b; audio_.click(); } ly += step;
    b = s.particles;  if (ui::toggle(ren_, {lx, ly, colW, th}, "PARTICLES", b, in_))  { s.particles = b; audio_.click(); } ly += step;
    b = s.grass;      if (ui::toggle(ren_, {lx, ly, colW, th}, "GRASS", b, in_))      { s.grass = b; audio_.click(); } ly += step;
    b = s.water;      if (ui::toggle(ren_, {lx, ly, colW, th}, "WATER", b, in_))      { s.water = b; audio_.click(); } ly += step;
    b = s.flowers;    if (ui::toggle(ren_, {lx, ly, colW, th}, "FLOWERS", b, in_))    { s.flowers = b; audio_.click(); } ly += step;
    b = s.sound;      if (ui::toggle(ren_, {lx, ly, colW, th}, "SOUND", b, in_))      { s.sound = b; audio_.click(); } ly += step + 14;
    float vol = s.volume;
    if (ui::sliderF(ren_, {lx, ly, colW, 18}, "VOLUME", vol, 0.0f, 1.0f, in_)) s.volume = vol;

    // ---------------- Right column: world + building mix ----------------
    int ry = cy0;
    font::draw(ren_, "WORLD (APPLY TO REBUILD)", rx, ry, 1, ui::textDim()); ry += 22;
    float ag = (float)s.numAgents;
    if (ui::sliderF(ren_, {rx, ry, colW, 18}, "AGENTS", ag, 50.0f, 6000.0f, in_)) s.numAgents = (int)ag;
    ry += 38;
    float cd = (float)s.cityDepth;
    if (ui::sliderF(ren_, {rx, ry, colW, 18}, "CITY DEPTH 1-10", cd, 1.0f, 10.0f, in_))
        s.cityDepth = std::max(1, std::min(10, (int)(cd + 0.5f)));
    ry += 42;

    font::draw(ren_, "BUILDING MIX (APPLY TO REBUILD)", rx, ry, 1, ui::textDim()); ry += 22;
    float fr = (float)s.buildingResidentialPct;
    if (ui::sliderF(ren_, {rx, ry, colW, 18}, "RESIDENTIAL", fr, 0.0f, 100.0f, in_))
        s.buildingResidentialPct = std::max(0, std::min(100, (int)(fr + 0.5f)));
    ry += 36;
    float fo = (float)s.buildingOfficePct;
    if (ui::sliderF(ren_, {rx, ry, colW, 18}, "OFFICE", fo, 0.0f, 100.0f, in_))
        s.buildingOfficePct = std::max(0, std::min(100, (int)(fo + 0.5f)));
    ry += 36;
    float fi = (float)s.buildingIndustryPct;
    if (ui::sliderF(ren_, {rx, ry, colW, 18}, "INDUSTRY", fi, 0.0f, 100.0f, in_))
        s.buildingIndustryPct = std::max(0, std::min(100, (int)(fi + 0.5f)));
    ry += 36;
    float fp = (float)s.buildingParkPct;
    if (ui::sliderF(ren_, {rx, ry, colW, 18}, "PARK", fp, 0.0f, 100.0f, in_))
        s.buildingParkPct = std::max(0, std::min(100, (int)(fp + 0.5f)));
    ry += 30;
    // Normalized readout so the player sees the effective mix (weights -> %).
    int mixSum = std::max(1, s.buildingResidentialPct + s.buildingOfficePct +
                             s.buildingIndustryPct + s.buildingParkPct);
    char mixbuf[80];
    std::snprintf(mixbuf, sizeof(mixbuf), "= HOUSE %d%%  OFFICE %d%%  IND %d%%  PARK %d%%",
                  s.buildingResidentialPct * 100 / mixSum, s.buildingOfficePct * 100 / mixSum,
                  s.buildingIndustryPct * 100 / mixSum,    s.buildingParkPct * 100 / mixSum);
    font::draw(ren_, mixbuf, rx, ry, 1, ui::accent());

    // ---------------- Bottom: apply / save buttons (full width) ----------------
    int by = box.y + ph - 52, bw = (pw - 60) / 2;
    SDL_Rect bApply{ box.x + 24, by, bw, 36 };
    SDL_Rect bBack { box.x + 24 + bw + 12, by, bw, 36 };
    if (ui::button(ren_, bApply, "REBUILD WORLD", in_, {120, 200, 140, 255}, 2)) {
        audio_.click(); settings().saveToFile(configPath());
        adjustWorldForDepth();
        world_.regenerate();
        syncSpriteSizes();
        view_.cam.snap(s.worldW * 0.5f, s.worldH * 0.5f, 0.55f);
        toast("Settings saved. World rebuilt.");
    }
    if (ui::button(ren_, bBack, "SAVE & GO BACK", in_, ui::accent(), 2)) {
        audio_.click();
        settings().saveToFile(configPath());
        state_ = prevState_;
        toast("Settings saved.");
    }
}

void Game::renderHelp() {
    dimScreen(190);
    auto& s = settings();
    int pw = 560, ph = 488;
    SDL_Rect box{ s.screenW / 2 - pw / 2, s.screenH / 2 - ph / 2, pw, ph };
    ui::panel(ren_, box);
    int x = box.x + 26, y = box.y + 20;
    font::draw(ren_, "HOW TO PLAY", box.x + pw / 2, y, 4, ui::accent(), Align::Center);
    y += 48;
    const char* lines[] = {
        "YOU ARE THE MAYOR OF A LIVING CITY.",
        "KEEP CITY SAFETY ABOVE ZERO UNTIL THE",
        "SURVIVE TIMER RUNS OUT TO WIN.",
        "",
        "DEPLOY POLICE (1) TO ARREST CRIMINALS.",
        "DEPLOY HEALERS (2) TO REDUCE STRESS.",
        "EACH ARREST PAYS +$50, EACH HEAL +$10,",
        "PLUS STEADY CITY INCOME OVER TIME.",
        "SCORE = ARRESTS*10 + HEALS*5 + TIME.",
        "",
        "BUILD: PICK HOUSE/OFFICE/FACTORY/PARK,",
        "THEN CLICK THE MAP TO PLACE A BUILDING.",
        "SANDBOX: UNLIMITED BUDGET, SPAWN ANYONE",
        "(KEYS 3-7), AND EDIT AGENT STATS LIVE.",
        "",
        "WASD / ARROWS / RIGHT-DRAG = PAN",
        "WHEEL = ZOOM    LEFT CLICK = SELECT",
        "[ ] = SLOWER / FASTER    SPACE = PAUSE",
        "R = NEW CITY             ESC = BACK",
    };
    for (const char* l : lines) {
        font::draw(ren_, l, x, y, 2, ui::textMain());
        y += 20;
    }
    SDL_Rect bBack{ box.x + pw / 2 - 90, box.y + ph - 50, 180, 36 };
    if (ui::button(ren_, bBack, "BACK", in_, ui::accent(), 2)) { audio_.click(); state_ = prevState_; }
}

void Game::renderGameOver() {
    dimScreen(180);
    int cx = settings().screenW / 2, cy = settings().screenH / 2;
    if (won_) {
        font::drawShadowed(ren_, "CITY SAVED!", cx, cy - 130, 8, {90, 220, 130, 255}, Align::Center);
    } else {
        font::drawShadowed(ren_, "CITY LOST", cx, cy - 130, 8, {230, 80, 70, 255}, Align::Center);
    }
    char sc[64]; std::snprintf(sc, sizeof(sc), "SCORE: %d", finalScore_);
    font::draw(ren_, sc, cx, cy - 60, 4, ui::textMain(), Align::Center);
    char st[96];
    std::snprintf(st, sizeof(st), "ARRESTS %ld   HEALS %ld   CRIMES %ld",
                  world_.stats.arrests, world_.stats.heals, world_.stats.crimes);
    font::draw(ren_, st, cx, cy - 20, 2, ui::textDim(), Align::Center);

    int bw = 280, bh = 46, gap = 14;
    SDL_Rect bAgain{ cx - bw / 2, cy + 30, bw, bh };
    SDL_Rect bMenu { cx - bw / 2, cy + 30 + (bh + gap), bw, bh };
    if (ui::button(ren_, bAgain, "PLAY AGAIN", in_, ui::accent(), 3))     { audio_.click(); startNewGame(); }
    if (ui::button(ren_, bMenu, "MAIN MENU", in_, {120, 140, 200, 255}, 3)) { audio_.click(); state_ = GState::Menu; fade_ = 0.6f; }
}

} // namespace cv
