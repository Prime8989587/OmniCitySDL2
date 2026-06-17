// Config.h — compile-time defaults and a runtime-tunable Settings struct.
// CristiVerse / LogOS Engine — SDL2 redesign (C++17, pure SDL2).
#pragma once
#include <string>

namespace cv {

// ---- Window / world defaults (can be overridden by config file) ----
struct Settings {
    int   screenW      = 1280;
    int   screenH      = 720;
    bool  fullscreen   = false;
    bool  vsync        = true;

    float worldW       = 2400.0f;   // world units ("meters")
    float worldH       = 2400.0f;

    int   numAgents    = 1500;      // can push to ~10000
    int   numBuildings = 64;
    int   numTrees     = 140;       // scattered decorative trees

    // Graphics quality toggles (for lower-end machines)
    bool  animations   = true;      // walk-bob, smoke, window flicker
    bool  shadows      = true;      // cast shadows under buildings/agents/trees
    bool  dayNight     = true;
    bool  particles    = true;
    bool  grass        = true;      // procedural textured grass ground
    bool  water        = true;      // ponds in park zones
    bool  flowers      = true;      // flower patches in park zones

    // Audio (procedural SDL audio, no asset files)
    bool  sound        = true;
    float volume       = 0.5f;      // 0..1

    // Loads key=value pairs from a config file if present.
    // Unknown keys are ignored; missing file leaves defaults intact.
    void loadFromFile(const std::string& path);
    void saveToFile(const std::string& path) const;
};

// Global access to the active settings (defined in Config.cpp).
Settings& settings();

} // namespace cv
