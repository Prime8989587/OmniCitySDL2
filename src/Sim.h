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
    std::vector<Particle>  particles;
    std::vector<FloatText> floats;
    std::vector<LogEntry>  log;
    Stats stats;
    SpatialGrid grid;

    int   tick = 0;
    float dayTime = 0.30f;   // 0..1 cycle; 0.25=morning, 0.5=noon, 0.75=dusk

    void regenerate();        // rebuild buildings + agents from settings
    void step(float dt);      // advance one simulation step by dt seconds
    void recomputeStats();

    // Spawn helpers (used by sim + UI feedback).
    void spawnBurst(Vec2 p, SDL_Color c, int n, float speed, bool gravity = false);
    void addFloat(Vec2 p, const std::string& t, SDL_Color c);
    void addLog(const std::string& t, SDL_Color c);

    int  pickAgentNear(float wx, float wy, float worldRadius) const;

private:
    void generateBuildings();
    void generateAgents();
    bool insideBuilding(float x, float y, BType* outType = nullptr) const;
};

} // namespace cv
