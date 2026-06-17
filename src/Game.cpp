#include "Game.h"
#include "Config.h"
#include "Font.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace cv {

static const float kSpeeds[] = {0.5f, 1.0f, 2.0f, 4.0f};
static const int   kNumSpeeds = 4;
static const int   HUD_H   = 48;
static const int   SIDE_W  = 308;

// =====================================================================
// Lifecycle
// =====================================================================
bool Game::init() {
    settings().loadFromFile("cristiverse.cfg");
    auto& s = settings();

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init error: %s", SDL_GetError());
        return false;
    }

    Uint32 flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    if (s.fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    win_ = SDL_CreateWindow("CristiVerse — LogOS Engine (SDL2)",
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

    audio_.init(); // soft-fails when no device

    world_.regenerate();

    view_.cam.snap(s.worldW * 0.5f, s.worldH * 0.5f, 0.55f);
    layoutView();
    state_ = GState::Menu;
    fade_ = 1.0f;
    return true;
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
    world_.regenerate();
    view_.cam.snap(s.worldW * 0.5f, s.worldH * 0.5f, 0.9f);
    layoutView();
    return true;
}

void Game::captureFrames(const char* path, int frames) {
    startNewGame();
    fade_ = 0.0f;
    view_.cam.snap(settings().worldW * 0.5f, settings().worldH * 0.5f, 0.9f);
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
    if (win_) settings().saveToFile("cristiverse.cfg"); // only persist for real sessions
    audio_.shutdown();
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

        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                settings().screenW = e.window.data1;
                settings().screenH = e.window.data2;
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
                bool inWorld = (state_ == GState::Playing) &&
                               e.button.x < view_.viewX + view_.viewW &&
                               e.button.y > view_.viewY;
                if (inWorld) {
                    if (tool_ != Tool::None) {
                        deployAt(e.button.x, e.button.y);
                    } else {
                        float wx, wy; view_.screenToWorld(e.button.x, e.button.y, wx, wy);
                        int id = world_.pickAgentNear(wx, wy, 14.0f / view_.cam.zoom);
                        selectedId_ = id;
                        if (id >= 0) audio_.select();
                    }
                }
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

        case SDL_KEYDOWN: {
            SDL_Keycode k = e.key.keysym.sym;
            if (k == SDLK_ESCAPE) {
                if (state_ == GState::Playing)       state_ = GState::Paused;
                else if (state_ == GState::Paused)   state_ = GState::Playing;
                else if (state_ == GState::Settings ||
                         state_ == GState::Help)     state_ = prevState_;
                else if (state_ == GState::GameOver) state_ = GState::Menu;
                else if (state_ == GState::Menu)     running_ = false;
            }
            if (state_ == GState::Playing) {
                if (k == SDLK_SPACE) state_ = GState::Paused;
                if (k == SDLK_r)     { startNewGame(); }
                if (k == SDLK_1)     { tool_ = (tool_ == Tool::Police) ? Tool::None : Tool::Police; }
                if (k == SDLK_2)     { tool_ = (tool_ == Tool::Healer) ? Tool::None : Tool::Healer; }
                if (k == SDLK_TAB)   { tool_ = Tool::None; }
                if (k == SDLK_LEFTBRACKET)  setSpeed(speedIndex_ - 1);
                if (k == SDLK_RIGHTBRACKET) setSpeed(speedIndex_ + 1);
                if (k == SDLK_h)     { prevState_ = state_; state_ = GState::Help; }
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
// Update
// =====================================================================
void Game::setSpeed(int idx) {
    speedIndex_ = std::clamp(idx, 0, kNumSpeeds - 1);
    simSpeed_ = kSpeeds[speedIndex_];
}

void Game::startNewGame() {
    auto& s = settings();
    world_.regenerate();
    view_.cam.snap(s.worldW * 0.5f, s.worldH * 0.5f, 0.55f);
    selectedId_ = -1;
    budget_ = 220.0f;
    safety_ = 100.0f;
    gameTimer_ = 0.0f;
    won_ = false;
    finalScore_ = 0;
    tool_ = Tool::None;
    setSpeed(1);
    state_ = GState::Playing;
    fade_ = 0.7f;
    toast("New city online. Keep it safe!");
}

void Game::deployAt(int sx, int sy) {
    float cost = (tool_ == Tool::Police) ? 100.0f : 80.0f;
    if (budget_ < cost) { toast("Not enough budget!"); audio_.alarm(); return; }
    float wx, wy; view_.screenToWorld(sx, sy, wx, wy);
    Agent a;
    a.id = (int)world_.agents.size();
    a.pos = { wx, wy };
    a.role = (tool_ == Tool::Police) ? Role::Police : Role::Healer;
    a.animPhase = frand(0.0f, 6.28f);
    world_.agents.push_back(a);
    budget_ -= cost;
    audio_.deploy();
    world_.spawnBurst({wx, wy}, roleColor(a.role), 12, 90.0f);
    world_.addLog(std::string("Deployed ") + roleName(a.role) + ".", roleColor(a.role));
    toast(std::string("Deployed ") + roleName(a.role));
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

        // Budget & safety dynamics.
        budget_ = std::min(620.0f, budget_ + 7.0f * dt);
        int living = 0;
        for (int i = 0; i < (int)Role::COUNT; ++i) living += world_.stats.count[i];
        int crime = world_.stats.count[(int)Role::Criminal] + world_.stats.count[(int)Role::Gang];
        float ratio = living > 0 ? (float)crime / living : 0.0f;
        float rate = 9.0f - ratio * 70.0f - std::max(0.0f, world_.stats.avgStress - 0.4f) * 40.0f;
        safety_ = clampf(safety_ + rate * dt, 0.0f, 100.0f);

        gameTimer_ += dt * simSpeed_;

        // Win / lose.
        if (safety_ <= 0.0f) {
            won_ = false;
            finalScore_ = (int)(world_.stats.arrests * 10 + world_.stats.heals * 5 + gameTimer_);
            state_ = GState::GameOver;
            audio_.lose();
        } else if (gameTimer_ >= goalTime_) {
            won_ = true;
            finalScore_ = (int)(world_.stats.arrests * 10 + world_.stats.heals * 5 +
                                gameTimer_ + safety_ * 3);
            state_ = GState::GameOver;
            audio_.win();
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
SDL_Color Game::skyTop() const {
    float b = dayBrightness();
    return scaleColor({26, 36, 44, 255}, b);
}
SDL_Color Game::skyBottom() const {
    float b = dayBrightness();
    return scaleColor({14, 20, 26, 255}, b);
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
    for (const auto& b : world_.buildings) renderBuilding(b);
    for (const auto& a : world_.agents) renderAgent(a);
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
    SDL_Rect v{ view_.viewX, view_.viewY, view_.viewW, view_.viewH };
    draw::vGradient(ren_, v, skyTop(), skyBottom());

    // Road grid in world space.
    float wx0, wy0, wx1, wy1;
    view_.screenToWorld(view_.viewX, view_.viewY, wx0, wy0);
    view_.screenToWorld(view_.viewX + view_.viewW, view_.viewY + view_.viewH, wx1, wy1);
    const float step = 200.0f;
    float b = dayBrightness();
    SDL_Color road = scaleColor({40, 48, 58, 255}, b);
    float startX = std::floor(wx0 / step) * step;
    float startY = std::floor(wy0 / step) * step;
    for (float x = startX; x <= wx1; x += step) {
        int sx, sy, sx2, sy2;
        view_.worldToScreen(x, wy0, sx, sy);
        view_.worldToScreen(x, wy1, sx2, sy2);
        draw::line(ren_, sx, view_.viewY, sx2, view_.viewY + view_.viewH, road);
    }
    for (float y = startY; y <= wy1; y += step) {
        int sx, sy, sx2, sy2;
        view_.worldToScreen(wx0, y, sx, sy);
        view_.worldToScreen(wx1, y, sx2, sy2);
        draw::line(ren_, view_.viewX, sy, view_.viewX + view_.viewW, sy2, road);
    }

    // World border.
    SDL_Rect border;
    if (view_.worldRectToScreen(0, 0, settings().worldW, settings().worldH, border))
        draw::rect(ren_, border, {70, 90, 120, 200});
}

void Game::renderBuilding(const Building& b) {
    SDL_Rect r;
    if (!view_.worldRectToScreen(b.pos.x, b.pos.y, b.w, b.h, r)) return;
    float bright = dayBrightness();

    // Shadow.
    if (settings().shadows) {
        SDL_Rect sh = r; sh.x += 5; sh.y += 6;
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 90);
        SDL_RenderFillRect(ren_, &sh);
    }

    SDL_Color base;
    switch (b.type) {
        case BType::Residential:   base = {86, 132, 92, 255};  break;
        case BType::Office:        base = {120, 134, 168, 255}; break;
        case BType::Industry:      base = {150, 116, 84, 255};  break;
        case BType::Park:          base = {54, 120, 64, 255};   break;
        case BType::PoliceStation: base = {52, 86, 150, 255};   break;
        case BType::Hospital:      base = {198, 206, 214, 255}; break;
        default:                   base = {120,120,120,255};    break;
    }
    // Variant tint.
    base = scaleColor(base, (0.85f + 0.05f * b.variant) * bright);

    if (b.type == BType::Park) {
        draw::roundedRect(ren_, r, std::min(10, r.w / 4), base);
        // Trees.
        int trees = std::max(1, (r.w * r.h) / 4000);
        unsigned seed = b.windowSeed;
        for (int i = 0; i < trees && i < 24; ++i) {
            seed = seed * 1103515245u + 12345u;
            int tx = r.x + 6 + (seed >> 8) % std::max(1, r.w - 12);
            seed = seed * 1103515245u + 12345u;
            int ty = r.y + 6 + (seed >> 8) % std::max(1, r.h - 12);
            int rad = std::max(2, r.w / 20);
            draw::fillCircle(ren_, tx, ty, rad, scaleColor({40, 100, 50, 255}, bright));
            draw::fillCircle(ren_, tx, ty, std::max(1, rad - 2), scaleColor({66, 140, 70, 255}, bright));
        }
        return;
    }

    // Body.
    SDL_RenderSetClipRect(ren_, nullptr); // ensure base fill ok
    SDL_Rect clip{ view_.viewX, view_.viewY, view_.viewW, view_.viewH };
    SDL_RenderSetClipRect(ren_, &clip);
    draw::fillRect(ren_, r, base);

    // Roof strip.
    SDL_Rect roof{ r.x, r.y, r.w, std::max(2, r.h / 8) };
    draw::fillRect(ren_, roof, scaleColor(base, 0.7f));

    // Windows (only when big enough on screen).
    if (r.w > 26 && r.h > 26) {
        int cell = std::max(8, (int)(14 * view_.cam.zoom));
        int pad = std::max(3, cell / 3);
        bool night = bright < 0.6f;
        unsigned seed = b.windowSeed;
        SDL_Color wallDark = scaleColor(base, 0.78f);
        SDL_Color winLit   = {255, 224, 140, 255};
        SDL_Color winDark  = scaleColor({30, 36, 48, 255}, bright);
        for (int wy = r.y + roof.h + pad; wy < r.y + r.h - pad; wy += cell) {
            for (int wx = r.x + pad; wx < r.x + r.w - pad; wx += cell) {
                seed = seed * 1103515245u + 12345u;
                bool lit = night && ((seed >> 16) & 7) < 3;
                // Subtle flicker.
                if (lit && settings().animations) {
                    float fl = std::sin(menuAnim_ * 3.0f + (seed & 63));
                    if (fl < -0.95f) lit = false;
                }
                SDL_Rect w{ wx, wy, std::max(2, cell - pad), std::max(2, cell - pad) };
                draw::fillRect(ren_, w, lit ? winLit : (((seed >> 8) & 1) ? winDark : wallDark));
            }
        }
    }

    // Type markers.
    if (b.type == BType::Hospital && r.w > 16 && r.h > 16) {
        int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        int s = std::max(3, r.w / 8);
        draw::fillRect(ren_, {cx - s / 3, cy - s, (2 * s) / 3, 2 * s}, {220, 60, 60, 255});
        draw::fillRect(ren_, {cx - s, cy - s / 3, 2 * s, (2 * s) / 3}, {220, 60, 60, 255});
    } else if (b.type == BType::PoliceStation && r.w > 16 && r.h > 16) {
        int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        int s = std::max(3, r.w / 7);
        draw::fillCircle(ren_, cx, cy, s, {235, 215, 90, 255});
        draw::fillCircle(ren_, cx, cy, std::max(1, s - 3), {52, 86, 150, 255});
    } else if (b.type == BType::Industry && r.w > 16) {
        // Chimney.
        SDL_Rect ch{ r.x + (int)(r.w * 0.62f), r.y - std::max(4, r.h / 6), std::max(3, r.w / 10), std::max(4, r.h / 5) };
        draw::fillRect(ren_, ch, scaleColor({90, 70, 56, 255}, bright));
    }

    // Outline.
    draw::rect(ren_, r, scaleColor(base, 0.5f));
}

void Game::renderAgent(const Agent& a) {
    if (!a.alive) return;
    int sx, sy;
    view_.worldToScreen(a.pos.x, a.pos.y, sx, sy);
    if (sx < view_.viewX - 8 || sx > view_.viewX + view_.viewW + 8 ||
        sy < view_.viewY - 8 || sy > view_.viewY + view_.viewH + 8) return;

    float zoom = view_.cam.zoom;
    int rad = (int)clampf(zoom * 4.5f, 2.0f, 16.0f);
    SDL_Color col = roleColor(a.role);

    // Far zoom: cheap points.
    if (rad <= 2) {
        SDL_Rect p{ sx - 1, sy - 1, 3, 3 };
        draw::fillRect(ren_, p, col);
        if (a.id == selectedId_) draw::rect(ren_, {sx - 3, sy - 3, 6, 6}, {255, 255, 0, 255});
        return;
    }

    // Walk bob.
    int bob = 0;
    if (settings().animations && (a.act == Act::Walk || a.act == Act::Flee))
        bob = (int)(std::sin(a.animPhase) * (rad * 0.25f));

    int cy = sy - bob;

    // Selection ring (pulsing).
    if (a.id == selectedId_) {
        float pulse = 1.0f + 0.2f * std::sin(menuAnim_ * 6.0f);
        draw::circleOutline(ren_, sx, cy, (int)(rad * 1.9f * pulse), {255, 255, 0, 255});
        draw::circleOutline(ren_, sx, cy, (int)(rad * 1.9f * pulse) + 1, {255, 255, 0, 160});
    }

    // Action flash ring.
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
        draw::circleOutline(ren_, sx, cy, (int)(rad * (1.6f + (1.0f - a.actFlash))), fc);
    }

    // Body + outline + facing highlight.
    draw::fillCircle(ren_, sx, cy, rad, col);
    draw::circleOutline(ren_, sx, cy, rad, scaleColor(col, 0.5f));
    if (rad >= 4) {
        int hx = sx + (int)(a.facing * rad * 0.4f);
        draw::fillCircle(ren_, hx, cy - rad / 4, std::max(1, rad / 3), scaleColor(col, 1.4f));
    }

    // Stress indicator (small bar above) when zoomed in.
    if (rad >= 6 && a.stress > 0.5f) {
        SDL_Rect bar{ sx - rad, cy - rad - 5, (int)(rad * 2 * a.stress), 2 };
        draw::fillRect(ren_, bar, {240, 80, 60, 220});
    }
}

void Game::renderParticles() {
    SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
    for (const auto& p : world_.particles) {
        int sx, sy;
        view_.worldToScreen(p.pos.x, p.pos.y, sx, sy);
        float t = clampf(p.life / p.maxLife, 0.0f, 1.0f);
        SDL_Color c = p.color;
        c.a = (Uint8)(c.a * t);
        int s = std::max(1, (int)(p.size * view_.cam.zoom));
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

    font::drawShadowed(ren_, "CRISTIVERSE", 14, 8, 3, ui::accent());
    font::draw(ren_, "LOGOS ENGINE", 14, 32, 1, ui::textDim());

    // Role chips.
    int x = 236;
    const char* labels[] = {"CIV", "CRIM", "POL", "GANG", "HEAL"};
    for (int i = 0; i < (int)Role::COUNT; ++i) {
        SDL_Color c = roleColor((Role)i);
        draw::fillCircle(ren_, x + 6, HUD_H / 2, 6, c);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%s %d", labels[i], world_.stats.count[i]);
        font::draw(ren_, buf, x + 18, HUD_H / 2 - 6, 2, ui::textMain());
        x += 24 + font::textWidth(buf, 2) + 16;
    }

    // Right side: day phase, fps.
    const char* phase = "DAY";
    if (s.dayNight) {
        float b = dayBrightness();
        phase = b > 0.75f ? "NOON" : b > 0.5f ? "DAY" : b > 0.4f ? "DUSK" : "NIGHT";
    }
    char rbuf[64];
    std::snprintf(rbuf, sizeof(rbuf), "%s   FPS %d   SPEED %.1fX", phase, (int)fps_, simSpeed_);
    font::draw(ren_, rbuf, s.screenW - 12, HUD_H / 2 - 6, 2, ui::textDim(), Align::Right);
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

    // --- Objective / meters ---
    font::draw(ren_, "OBJECTIVE", x, y, 2, ui::accent()); y += 22;
    int remain = std::max(0, (int)(goalTime_ - gameTimer_));
    char ob[64]; std::snprintf(ob, sizeof(ob), "SURVIVE %02d:%02d", remain / 60, remain % 60);
    font::draw(ren_, ob, x, y, 2, ui::textMain()); y += 22;

    // Safety meter.
    font::draw(ren_, "CITY SAFETY", x, y, 1, ui::textDim()); y += 12;
    SDL_Rect sm{ x, y, w, 14 };
    draw::roundedRect(ren_, sm, 4, {40, 46, 60, 255});
    SDL_Color safeCol = lerpColor({230, 70, 60, 255}, {70, 210, 110, 255}, safety_ / 100.0f);
    draw::roundedRect(ren_, { x, y, (int)(w * safety_ / 100.0f), 14 }, 4, safeCol);
    y += 22;

    // Budget.
    char bb[48]; std::snprintf(bb, sizeof(bb), "BUDGET: %d C", (int)budget_);
    font::draw(ren_, bb, x, y, 2, {255, 215, 90, 255}); y += 26;

    // --- Deploy tools ---
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
    if (tool_ != Tool::None) {
        std::string t = std::string("ACTIVE: ") + (tool_ == Tool::Police ? "POLICE" : "HEALER") + " (TAB CANCEL)";
        font::draw(ren_, t, x, y, 1, ui::accent());
    }
    y += 16;

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

    // --- Selected agent info ---
    SDL_Rect infoArea{ x, y, w, 96 };
    renderInfoPanel(infoArea);
    y += 104;

    // --- Minimap ---
    int mmH = std::min(w, side.y + side.h - y - 150);
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
    if (selectedId_ < 0 || selectedId_ >= (int)world_.agents.size()) {
        font::draw(ren_, "NO SELECTION", x, y, 2, ui::textDim());
        font::draw(ren_, "CLICK AN AGENT", x, y + 20, 1, ui::textDim());
        return;
    }
    const Agent& a = world_.agents[selectedId_];
    SDL_Color rc = roleColor(a.role);
    draw::fillCircle(ren_, x + 8, y + 8, 8, a.alive ? rc : SDL_Color{90, 90, 90, 255});
    char hdr[48]; std::snprintf(hdr, sizeof(hdr), "%s #%d", roleName(a.role), a.id);
    font::draw(ren_, hdr, x + 24, y + 2, 2, ui::textMain());
    if (!a.alive) { font::draw(ren_, "STATUS: DECEASED", x, y + 24, 1, {220, 90, 90, 255}); return; }

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
    const char* h = "WASD/DRAG PAN   WHEEL ZOOM   1 POLICE  2 HEALER   SPACE PAUSE   H HELP   R RESET";
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
    font::drawShadowed(ren_, "CRISTIVERSE", cx, (int)(s.screenH * 0.18f + bob), 10, ui::accent(), Align::Center);
    font::draw(ren_, "LOGOS ENGINE  -  SDL2 EDITION", cx, (int)(s.screenH * 0.18f) + 90, 2, ui::textDim(), Align::Center);

    // Buttons.
    int bw = 280, bh = 46, gap = 14;
    int by = (int)(s.screenH * 0.45f);
    SDL_Rect bNew { cx - bw / 2, by, bw, bh };
    SDL_Rect bSet { cx - bw / 2, by + (bh + gap), bw, bh };
    SDL_Rect bHelp{ cx - bw / 2, by + 2 * (bh + gap), bw, bh };
    SDL_Rect bQuit{ cx - bw / 2, by + 3 * (bh + gap), bw, bh };

    if (ui::button(ren_, bNew, "NEW GAME", in_, ui::accent(), 3))      { audio_.click(); startNewGame(); }
    if (ui::button(ren_, bSet, "SETTINGS", in_, {120, 140, 200, 255}, 3)) { audio_.click(); prevState_ = GState::Menu; state_ = GState::Settings; }
    if (ui::button(ren_, bHelp, "HOW TO PLAY", in_, {120, 140, 200, 255}, 3)) { audio_.click(); prevState_ = GState::Menu; state_ = GState::Help; }
    if (ui::button(ren_, bQuit, "QUIT", in_, {180, 90, 90, 255}, 3))   { audio_.click(); running_ = false; }

    font::draw(ren_, "A LIVING-CITY SIMULATION SANDBOX", cx, s.screenH - 40, 1, ui::textDim(), Align::Center);
}

void Game::renderPause() {
    dimScreen(150);
    int cx = settings().screenW / 2, cy = settings().screenH / 2;
    font::drawShadowed(ren_, "PAUSED", cx, cy - 110, 8, ui::textMain(), Align::Center);

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
    int pw = 440, ph = 470;
    SDL_Rect box{ s.screenW / 2 - pw / 2, s.screenH / 2 - ph / 2, pw, ph };
    ui::panel(ren_, box);
    int x = box.x + 24, y = box.y + 20, w = pw - 48;
    font::draw(ren_, "SETTINGS", box.x + pw / 2, y, 4, ui::accent(), Align::Center);
    y += 44;

    int th = 30, gap = 10;
    bool b;
    b = s.animations; if (ui::toggle(ren_, {x, y, w, th}, "ANIMATIONS", b, in_)) { s.animations = b; audio_.click(); } y += th + gap;
    b = s.shadows;    if (ui::toggle(ren_, {x, y, w, th}, "SHADOWS", b, in_))    { s.shadows = b; audio_.click(); } y += th + gap;
    b = s.dayNight;   if (ui::toggle(ren_, {x, y, w, th}, "DAY/NIGHT", b, in_))  { s.dayNight = b; audio_.click(); } y += th + gap;
    b = s.particles;  if (ui::toggle(ren_, {x, y, w, th}, "PARTICLES", b, in_))  { s.particles = b; audio_.click(); } y += th + gap;
    b = s.sound;      if (ui::toggle(ren_, {x, y, w, th}, "SOUND", b, in_))      { s.sound = b; audio_.click(); } y += th + gap + 14;

    float vol = s.volume;
    if (ui::sliderF(ren_, {x, y, w, 20}, "VOLUME", vol, 0.0f, 1.0f, in_)) s.volume = vol;
    y += 40;
    float ag = (float)s.numAgents;
    if (ui::sliderF(ren_, {x, y, w, 20}, "AGENTS (APPLY TO REBUILD)", ag, 50.0f, 6000.0f, in_)) s.numAgents = (int)ag;
    y += 40;
    float bn = (float)s.numBuildings;
    if (ui::sliderF(ren_, {x, y, w, 20}, "BUILDINGS (APPLY TO REBUILD)", bn, 8.0f, 160.0f, in_)) s.numBuildings = (int)bn;
    y += 44;

    SDL_Rect bApply{ x, y, w / 2 - 6, 36 };
    SDL_Rect bBack { x + w / 2 + 6, y, w / 2 - 6, 36 };
    if (ui::button(ren_, bApply, "APPLY + NEW WORLD", in_, {120, 200, 140, 255}, 1)) {
        audio_.click(); world_.regenerate();
        view_.cam.snap(s.worldW * 0.5f, s.worldH * 0.5f, 0.55f);
        toast("World rebuilt");
    }
    if (ui::button(ren_, bBack, "BACK", in_, ui::accent(), 2)) { audio_.click(); state_ = prevState_; }
}

void Game::renderHelp() {
    dimScreen(190);
    auto& s = settings();
    int pw = 560, ph = 460;
    SDL_Rect box{ s.screenW / 2 - pw / 2, s.screenH / 2 - ph / 2, pw, ph };
    ui::panel(ren_, box);
    int x = box.x + 26, y = box.y + 20;
    font::draw(ren_, "HOW TO PLAY", box.x + pw / 2, y, 4, ui::accent(), Align::Center);
    y += 48;
    const char* lines[] = {
        "YOU ARE THE MAYOR OF A LIVING CITY.",
        "AGENTS ROAM, INTERACT, COMMIT CRIMES,",
        "GET ARRESTED, AND GET HEALED ON THEIR OWN.",
        "",
        "GOAL: KEEP CITY SAFETY ABOVE ZERO",
        "UNTIL THE SURVIVE TIMER RUNS OUT.",
        "",
        "DEPLOY POLICE (1) TO REDUCE CRIME.",
        "DEPLOY HEALERS (2) TO REDUCE STRESS.",
        "CLICK THE MAP TO PLACE THEM (COSTS BUDGET).",
        "BUDGET REGENERATES OVER TIME.",
        "",
        "CONTROLS:",
        "  WASD / ARROWS / RIGHT-DRAG = PAN",
        "  MOUSE WHEEL = ZOOM",
        "  LEFT CLICK = SELECT AGENT",
        "  [ ] = SLOWER / FASTER     SPACE = PAUSE",
        "  R = NEW CITY              ESC = BACK",
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
