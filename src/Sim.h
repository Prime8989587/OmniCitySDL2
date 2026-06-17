// Sim.h — world state, agents, buildings, particles, and the simulation step.
#pragma once
#include <vector>
#include <string>
#include <SDL.h>
#include "Math.h"

namespace cv {

enum class Role : int { Civil, Criminal, Police, Gang, Healer, COUNT };

enum class Act : int { Idle, Walk, Rob, Heal, Arrest, Flee, Fight };

const char* roleName(Role r);
SDL_Color   roleColor(Role r);

enum class BType : int {
    Residential = 0, Office, Industry, Park, PoliceStation, Hospital, COUNT
};
const char* btypeName(BType t);

enum class TreeType : int { Deciduous = 0, Pine, Willow, Dead, COUNT };

// Shared gameplay economy constants (used by the sim for floating text and by
// the game loop to award budget — keep the two in sync).
namespace econ {
    constexpr int kBountyArrest = 50;  // reward per criminal/gang arrested
    constexpr int kBountyHeal   = 10;  // reward per heal milestone
}

// Fraction of a building's height (from the bottom) that is physically solid.
// The upper part is walkable "behind" the building, enabling occlusion x-ray.
constexpr float kBuildingSolidFrac = 0.60f;

struct Agent {
    int   id = 0;
    Vec2  pos, vel;
    Role  role = Role::Civil;
    Act   act  = Act::Idle;
    bool  alive = true;

    float stress = 0.2f;    // 0..1
    float money  = 100.0f;
    float health = 1.0f;    // 0..1

    float animPhase = 0.0f; // walk-cycle phase
    float facing    = 1.0f; // +1 right, -1 left
    float actFlash  = 0.0f; // >0 => currently performing a visible action
    int   targetId  = -1;

    bool  sleeping  = false; // civilians sleep inside their home at night
    int   home      = -1;    // index into buildings (where this agent sleeps)
};

struct Building {
    Vec2  pos;
    float w = 0, h = 0;
    BType type = BType::Residential;
    int   variant = 0;       // style variant
    unsigned windowSeed = 0; // deterministic window light pattern
    float smokeAccum = 0;    // industry smoke timer
    float damage = 0.0f;     // 0 intact .. 1 wrecked
};

struct Particle {
    Vec2  pos, vel;
    float life = 0, maxLife = 1;
    float size = 2;
    SDL_Color color{255,255,255,255};
    bool  gravity = false;
};

struct Tree {
    Vec2     pos;            // trunk base in world space
    TreeType type = TreeType::Deciduous;
    float    height = 30.0f; // world units
    unsigned seed = 0;       // per-tree shape variation
};

// A purely decorative pond placed inside a park zone (non-solid).
struct Water {
    Vec2     pos;            // center in world space
    float    rx = 40.0f;     // half-width  (world units)
    float    ry = 28.0f;     // half-height (world units)
    unsigned seed = 0;       // shimmer / wave variation
};

// A small decorative flower (or flower in a cluster) inside a park (non-solid).
struct Flower {
    Vec2      pos;           // base of the stem in world space
    SDL_Color color{255, 80, 80, 255};
    float     size = 4.0f;   // petal radius in world units
};

struct FloatText {
    Vec2  pos;
    std::string text;
    float life = 0, maxLife = 1.5f;
    SDL_Color color{255,255,255,255};
};

struct LogEntry {
    int tick;
    std::string text;
    SDL_Color color;
};

struct Stats {
    int  count[(int)Role::COUNT] = {0};
    int  dead = 0;
    long crimes = 0;
    long heals = 0;
    long arrests = 0;
    long moneyStolen = 0;
    float avgStress = 0.0f;
};

// Uniform-grid spatial index over agents for O(1)-ish neighbor queries.
class SpatialGrid {
public:
    void build(const std::vector<Agent>& agents, float worldW, float worldH, float cell);
    // Visit candidate agent indices within `radius` of (x,y).
    template <class Fn>
    void query(float x, float y, float radius, Fn&& fn) const {
        if (cells_.empty()) return; // not built yet
        int c0 = clampi((int)((x - radius) / cell_), 0, cols_ - 1);
        int c1 = clampi((int)((x + radius) / cell_), 0, cols_ - 1);
        int r0 = clampi((int)((y - radius) / cell_), 0, rows_ - 1);
        int r1 = clampi((int)((y + radius) / cell_), 0, rows_ - 1);
        for (int r = r0; r <= r1; ++r)
            for (int c = c0; c <= c1; ++c)
                for (int idx : cells_[r * cols_ + c]) fn(idx);
    }
private:
    static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
    float cell_ = 64.0f;
    int cols_ = 1, rows_ = 1;
    std::vector<std::vector<int>> cells_;
};

class World {
public:
    std::vector<Building>  buildings;
    std::vector<Agent>     agents;
    std::vector<Tree>      trees;
    std::vector<Water>     waters;
    std::vector<Flower>    flowers;
    std::vector<Particle>  particles;
    std::vector<FloatText> floats;
    std::vector<LogEntry>  log;
    Stats stats;
    SpatialGrid grid;

    int   tick = 0;
    float dayTime = 0.30f;   // 0..1 cycle; 0.0=midnight, 0.5=noon

    // Hour of day in [0,24). dayTime 0.5 == 12:00 (brightest / noon).
    float hourOfDay() const { return dayTime * 24.0f; }

    void regenerate();          // rebuild buildings + trees + agents
    void regenerateAgentsOnly(); // keep buildings/trees, respawn agents
    void step(float dt);        // advance one simulation step by dt seconds
    void recomputeStats();

    // Spawn helpers (used by sim + UI feedback).
    void spawnBurst(Vec2 p, SDL_Color c, int n, float speed, bool gravity = false);
    void addFloat(Vec2 p, const std::string& t, SDL_Color c);
    void addLog(const std::string& t, SDL_Color c);

    int  pickAgentNear(float wx, float wy, float worldRadius) const;
    int  nearestHome(float wx, float wy) const; // nearest residential/office

private:
    void generateBuildings();
    void generateTrees();
    void generateParkDecor();   // ponds + flowers inside park zones
    void generateAgents();
    bool insideBuilding(float x, float y, BType* outType = nullptr) const;
};

} // namespace cv
