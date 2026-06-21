// Config.h — compile-time defaults and a runtime-tunable Settings struct.
// CristiVerse / LogOS Engine — SDL2 redesign (C++17, pure SDL2).
#pragma once
#include <string>
#include <algorithm>

namespace cv {

// ---- Window / world defaults (can be overridden by config file) ----
struct Settings {
    int   screenW      = 1280;
    int   screenH      = 720;
    bool  fullscreen   = false;
    bool  vsync        = true;

    float worldW       = 2400.0f;   // world units ("meters") — derived from cityDepth at start
    float worldH       = 2400.0f;

    int   numAgents    = 1500;      // can push to ~10000
    int   numTrees     = 24;        // scattered decorative trees

    // City Depth: 1–10 slider controlling building count in a small fixed world.
    // depth1 ≈ 10 buildings, depth7 ≈ 21, depth10 ≈ 30. Each building renders
    // at native sprite resolution. Agents spawn based on population, not buildings.
    int   cityDepth    = 7;         // 1..10

    // Building-type distribution for auto-generated cities. These are *weights*
    // (not required to sum to 100); generation normalizes them, so only the
    // relative proportions matter. Police Station + Hospital are always added as
    // unique civic anchors and are not part of this mix.
    int   buildingResidentialPct = 50;   // homes
    int   buildingOfficePct      = 25;   // office towers
    int   buildingIndustryPct    = 15;   // industry
    int   buildingParkPct        = 10;   // parks / green space

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

// ---- City Depth geometry (shared by the simulation and the renderer) ----
// These MUST agree between world generation and rendering so roads, building
// blocks, vehicle lanes, and the world bounds all line up on the same grid.

// Square world side length for a given depth. Higher depth => smaller, denser
// world. depth 5 == 1800 (compact default); depth 1 ~= 2376; depth 10 ~= 1080.
inline float worldSizeForDepth(int depth) {
    depth = std::max(1, std::min(10, depth));
    float scale = 1.0f - (depth - 5) * 0.08f;
    return 1800.0f * scale;
}

// Spacing between roads (and thus the size of each city block) for a depth.
// depth 5 == 200; lower depth widens it (sparser), higher tightens it.
inline float roadSpacingForDepth(int depth) {
    depth = std::max(1, std::min(10, depth));
    float step = 200.0f + (5 - depth) * 22.0f;   // depth1=288 .. depth10=90
    return std::max(90.0f, step);
}

// Probability that any given city block actually receives a building. Lower
// depth leaves more empty lots (organic gaps / scattered neighborhoods); higher
// depth fills almost everything for a packed downtown. This is what breaks up
// the sterile "every block filled" grid into a city with natural character.
// depth1 = 0.25, depth5 = 0.57, depth10 = 0.97.
inline float fillProbabilityForDepth(int depth) {
    depth = std::max(1, std::min(10, depth));
    float p = 0.25f + (depth - 1) * 0.08f;
    return std::max(0.05f, std::min(1.0f, p));
}

} // namespace cv
