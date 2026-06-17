// Game.h — top-level application: window, state machine, input, rendering.
#pragma once
#include <SDL.h>
#include <string>
#include "Sim.h"
#include "Render.h"
#include "UI.h"
#include "Audio.h"

namespace cv {

enum class GState { Menu, Playing, Paused, Settings, GameOver, Help };

// Survival = the timed mayor challenge; Sandbox = free creative play.
enum class GameMode { Survival, Sandbox };

// Deployable / spawnable tool the player can place by clicking the world.
// Police/Healer are the survival deploys; the Spawn* tools are sandbox-only.
enum class Tool { None, Police, Healer,
                  SpawnCivil, SpawnCriminal, SpawnPolice, SpawnHealer, SpawnGang };

class Game {
public:
    bool init();
    void run();
    void shutdown();

    // Headless rendering for automated screenshots / CI smoke tests.
    bool initHeadless();
    void captureFrames(const char* path, int frames);

private:
    SDL_Window*   win_ = nullptr;
    SDL_Renderer* ren_ = nullptr;
    SDL_Surface*  shotSurface_ = nullptr;   // used only in headless capture mode
    Audio audio_;

    World world_;
    View  view_;
    InputState in_;

    GState state_ = GState::Menu;
    GState prevState_ = GState::Menu;   // for returning from Settings/Help

    int   selectedId_ = -1;
    int   speedIndex_ = 1;              // index into kSpeeds
    float simSpeed_ = 1.0f;

    bool  running_ = true;
    bool  dragging_ = false;
    int   lastMx_ = 0, lastMy_ = 0;

    // Mayor gameplay loop.
    GameMode mode_ = GameMode::Survival;
    float budget_ = 200.0f;
    float safety_ = 100.0f;            // 0 => lose
    float gameTimer_ = 0.0f;           // seconds survived
    float goalTime_  = 180.0f;         // survive this long => win
    bool  won_ = false;
    int   finalScore_ = 0;
    Tool  tool_ = Tool::None;

    // Economy bookkeeping (bounties awarded from stat deltas).
    long  prevArrests_ = 0;
    long  prevHeals_   = 0;
    float incomeTimer_ = 0.0f;         // passive city-tax accumulator
    bool  showGrid_    = false;        // sandbox placement grid overlay

    // Per-frame scratch: which buildings are made see-through by agents behind.
    std::vector<unsigned char> buildingOccluded_;

    // Presentation.
    float fade_ = 1.0f;                // screen fade-in (1->0)
    float menuAnim_ = 0.0f;            // animation clock for menu
    float toast_ = 0.0f;               // status toast timer
    std::string toastMsg_;
    float simAccum_ = 0.0f;            // fixed-step accumulator
    float fps_ = 0.0f;                 // smoothed frames-per-second
    int   hoverBuilding_ = -1;         // building under cursor (tooltip)

    // ---- lifecycle ----
    void handleEvents();
    void update(float dt);
    void render();

    // ---- world rendering ----
    void layoutView();
    void renderWorld();
    void renderGround();
    void renderGrid();                              // sandbox placement overlay
    void renderBuilding(const Building& b, Uint8 alpha);
    void renderTree(const Tree& t);
    void renderAgent(const Agent& a);
    void renderParticles();
    void renderFloats();
    void renderDayNight();

    // ---- HUD / panels ----
    void renderHUD();
    void renderSidebar();
    void renderInfoPanel(const SDL_Rect& area);
    void renderEventLog(const SDL_Rect& area);
    void renderMinimap(const SDL_Rect& area);
    void renderHints();
    void renderToast();

    // ---- overlay screens ----
    void renderMenu();
    void renderPause();
    void renderSettingsScreen();
    void renderGameOver();
    void renderHelp();
    void dimScreen(Uint8 alpha);

    // ---- helpers ----
    void startNewGame();
    void startSandbox();
    void setSpeed(int idx);
    void deployAt(int sx, int sy);
    void toast(const std::string& m) { toastMsg_ = m; toast_ = 2.5f; }
    float dayBrightness() const;       // 0..1 based on world_.dayTime
    SDL_Color skyTop() const;
    SDL_Color skyBottom() const;
    SDL_Color windowColor() const;     // lit-window tint for current time of day
    int   liveScore() const;           // projected score from current stats
};

} // namespace cv
