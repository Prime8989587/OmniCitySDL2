#include "Sim.h"
#include "Config.h"
#include <cstdio>

namespace cv {

const char* roleName(Role r) {
    switch (r) {
        case Role::Civil:    return "Civilian";
        case Role::Criminal: return "Criminal";
        case Role::Police:   return "Police";
        case Role::Gang:     return "Gang";
        case Role::Healer:   return "Healer";
        default:             return "?";
    }
}

SDL_Color roleColor(Role r) {
    switch (r) {
        case Role::Civil:    return {205, 212, 224, 255};
        case Role::Criminal: return {232,  64,  52, 255};
        case Role::Police:   return { 64, 144, 255, 255};
        case Role::Gang:     return {186,  72, 214, 255};
        case Role::Healer:   return { 66, 220, 120, 255};
        default:             return {255, 255, 255, 255};
    }
}

const char* btypeName(BType t) {
    switch (t) {
        case BType::Residential:   return "Residential";
        case BType::Office:        return "Office Tower";
        case BType::Industry:      return "Industry";
        case BType::Park:          return "Park";
        case BType::PoliceStation: return "Police Station";
        case BType::Hospital:      return "Hospital";
        default:                   return "?";
    }
}

// ---------------- SpatialGrid ----------------
void SpatialGrid::build(const std::vector<Agent>& agents, float worldW, float worldH, float cell) {
    cell_ = cell;
    cols_ = std::max(1, (int)std::ceil(worldW / cell));
    rows_ = std::max(1, (int)std::ceil(worldH / cell));
    cells_.assign((size_t)cols_ * rows_, {});
    for (int i = 0; i < (int)agents.size(); ++i) {
        const Agent& a = agents[i];
        if (!a.alive) continue;
        int c = clampi((int)(a.pos.x / cell_), 0, cols_ - 1);
        int r = clampi((int)(a.pos.y / cell_), 0, rows_ - 1);
        cells_[(size_t)r * cols_ + c].push_back(i);
    }
}

// ---------------- World ----------------
void World::regenerate() {
    tick = 0;
    dayTime = 0.30f;
    particles.clear();
    floats.clear();
    log.clear();
    generateBuildings();
    generateTrees();
    generateParkDecor();
    generateAgents();
    recomputeStats();
    addLog("World generated.", {180, 220, 255, 255});
}

void World::regenerateAgentsOnly() {
    particles.clear();
    floats.clear();
    generateAgents();
    recomputeStats();
    addLog("Population respawned.", {180, 220, 255, 255});
}

// Weighted pick among the four mix types (residential / office / industry /
// park) using the configured building-mix percentages.
static BType pickMixType() {
    const auto& s = settings();
    int rp = std::max(0, s.buildingResidentialPct);
    int op = std::max(0, s.buildingOfficePct);
    int ip = std::max(0, s.buildingIndustryPct);
    int pp = std::max(0, s.buildingParkPct);
    int sum = rp + op + ip + pp;
    if (sum <= 0) return BType::Residential;
    int roll = irand(0, sum - 1);
    if (roll < rp) return BType::Residential;
    roll -= rp;
    if (roll < op) return BType::Office;
    roll -= op;
    if (roll < ip) return BType::Industry;
    return BType::Park;
}

// Default footprint per building type. These are fallbacks used at generation
// time before syncSpriteSizes() reads the actual sprite dimensions. Parks stay
// small lawn-sized; all other buildings default to native sprite size (48×48).
void buildingFootprint(BType t, float& w, float& h) {
    if (t == BType::Park) {
        w = 32.0f; h = 32.0f;  // parks are small lawns
    } else {
        w = 48.0f; h = 48.0f;  // default building size (overridden by syncSpriteSizes)
    }
}

void World::generateBuildings() {
    const auto& s = settings();
    buildings.clear();

    auto finishMeta = [&](Building& b) {
        b.variant = irand(0, 3);
        b.windowSeed = (unsigned)irand(1, 1 << 30);
    };

    // Free placement: buildings scatter at random positions anywhere in the world
    // with FREE OVERLAP allowed (no collision testing). Y-sort depth-ordering
    // layers them naturally into an organic skyline. City Depth controls count
    // (~10–40 buildings for depth 1–10, with ~21 at depth 7).
    const float inset = 8.0f;

    // Civic anchors first (PoliceStation + Hospital).
    for (int i = 0; i < 2; ++i) {
        BType type = (i == 0) ? BType::PoliceStation : BType::Hospital;
        float w, h; buildingFootprint(type, w, h);
        float px = frand(inset, s.worldW - inset - w);
        float py = frand(inset, s.worldH - inset - h);
        Building b;
        b.type = type;
        b.w = w; b.h = h;
        b.pos = { px, py };
        finishMeta(b);
        buildings.push_back(b);
    }

    // City Depth → building count: depth1 ≈ 10, depth7 ≈ 21, depth10 ≈ 30.
    int target = std::clamp(s.cityDepth * 3, 10, 40);

    for (int i = 0; i < target; ++i) {
        BType type = pickMixType();
        float w, h; buildingFootprint(type, w, h);
        float px = frand(inset, s.worldW - inset - w);
        float py = frand(inset, s.worldH - inset - h);
        Building b;
        b.type = type;
        b.w = w; b.h = h;
        b.pos = { px, py };
        finishMeta(b);
        buildings.push_back(b);
    }
}

void World::generateTrees() {
    const auto& s = settings();
    trees.clear();
    // Plant trees in the small fixed world. No depth-scaling bonus needed since
    // the world is tiny and buildings are clustered.
    int n = std::max(0, s.numTrees);
    trees.reserve(n);
    for (int i = 0; i < n; ++i) {
        Tree t;
        // A few attempts to avoid landing inside a solid building.
        bool placed = false;
        for (int attempt = 0; attempt < 6 && !placed; ++attempt) {
            float x = frand(60.0f, s.worldW - 60.0f);
            float y = frand(60.0f, s.worldH - 60.0f);
            bool blocked = false;
            for (const auto& b : buildings) {
                if (b.type == BType::Park) continue; // trees welcome in parks
                if (x >= b.pos.x - 6 && x <= b.pos.x + b.w + 6 &&
                    y >= b.pos.y - 6 && y <= b.pos.y + b.h + 6) { blocked = true; break; }
            }
            if (!blocked) { t.pos = {x, y}; placed = true; }
        }
        if (!placed) continue;
        t.type   = (TreeType)irand(0, (int)TreeType::COUNT - 1);
        t.height = frand(24.0f, 46.0f);
        t.seed   = (unsigned)irand(1, 1 << 30);
        trees.push_back(t);
    }
}

void World::generateParkDecor() {
    waters.clear();
    flowers.clear();
    // Flower palette — warm/cool mix so patches look varied and natural.
    const SDL_Color palette[] = {
        {232,  76,  76, 255},  // red
        {248, 208,  72, 255},  // yellow
        {178,  96, 220, 255},  // purple
        {244, 244, 250, 255},  // white
        {244, 140, 196, 255},  // pink
        {110, 150, 246, 255},  // cornflower blue
    };
    const int paletteN = (int)(sizeof(palette) / sizeof(palette[0]));

    for (const auto& b : buildings) {
        if (b.type != BType::Park) continue;
        float cx = b.pos.x + b.w * 0.5f;
        float cy = b.pos.y + b.h * 0.5f;

        // --- Pond: one lake centered in the park (skip very small parks). ---
        Water pond;
        bool hasPond = false;
        if (b.w > 120.0f && b.h > 120.0f) {
            pond.rx = b.w * frand(0.22f, 0.32f);
            pond.ry = b.h * frand(0.18f, 0.26f);
            pond.pos = { cx + frand(-b.w * 0.08f, b.w * 0.08f),
                         cy + frand(-b.h * 0.08f, b.h * 0.08f) };
            pond.seed = (unsigned)irand(1, 1 << 30);
            waters.push_back(pond);
            hasPond = true;
        }

        // --- Flower clusters scattered across the lawn, avoiding the pond. ---
        int clusters = irand(10, 20);
        for (int c = 0; c < clusters; ++c) {
            float px = 0, py = 0; bool ok = false;
            for (int attempt = 0; attempt < 5 && !ok; ++attempt) {
                px = frand(b.pos.x + 12.0f, b.pos.x + b.w - 12.0f);
                py = frand(b.pos.y + 12.0f, b.pos.y + b.h - 12.0f);
                if (hasPond) {
                    float dx = (px - pond.pos.x) / (pond.rx + 10.0f);
                    float dy = (py - pond.pos.y) / (pond.ry + 10.0f);
                    if (dx * dx + dy * dy < 1.0f) continue; // inside/near pond
                }
                ok = true;
            }
            if (!ok) continue;
            SDL_Color base = palette[irand(0, paletteN - 1)];
            int n = irand(3, 5);
            for (int f = 0; f < n; ++f) {
                Flower fl;
                fl.pos = { px + frand(-8.0f, 8.0f), py + frand(-8.0f, 8.0f) };
                // Slight per-flower hue jitter within the cluster's color.
                fl.color = scaleColor(base, frand(0.85f, 1.1f));
                fl.color.a = 255;
                fl.size = frand(3.0f, 5.0f);
                flowers.push_back(fl);
            }
        }
    }
}

int World::nearestHome(float wx, float wy) const {
    int best = -1; float bestD2 = 1e18f;
    for (int i = 0; i < (int)buildings.size(); ++i) {
        const Building& b = buildings[i];
        if (b.type != BType::Residential && b.type != BType::Office) continue;
        float cx = b.pos.x + b.w * 0.5f, cy = b.pos.y + b.h * 0.5f;
        float d2 = dist2(wx, wy, cx, cy);
        if (d2 < bestD2) { bestD2 = d2; best = i; }
    }
    return best;
}

void World::generateAgents() {
    const auto& s = settings();
    agents.clear();
    int N = std::max(10, s.numAgents);
    agents.reserve(N);

    int nCriminal = N / 10;
    int nPolice   = N / 18;
    int nGang     = N / 28;
    int nHealer   = N / 24;

    auto push = [&](Role r) {
        Agent a;
        a.id = (int)agents.size();
        a.role = r;
        a.stress = frand(0.05f, 0.35f);
        a.money  = frand(40.0f, 160.0f);
        a.animPhase = frand(0.0f, 6.28f);

        // Find a valid spawn position that doesn't start inside a building.
        bool placed = false;
        for (int attempt = 0; attempt < 8 && !placed; ++attempt) {
            a.pos = { frand(80.0f, s.worldW - 80.0f), frand(80.0f, s.worldH - 80.0f) };
            if (!insideBuilding(a.pos.x, a.pos.y)) { placed = true; }
        }
        if (!placed) a.pos = { s.worldW * 0.5f, s.worldH * 0.5f }; // fallback to center

        a.vel = { frand(-15.0f, 15.0f), frand(-15.0f, 15.0f) };
        if (r == Role::Civil) a.home = nearestHome(a.pos.x, a.pos.y);
        agents.push_back(a);
    };

    for (int i = 0; i < nCriminal; ++i) push(Role::Criminal);
    for (int i = 0; i < nPolice;   ++i) push(Role::Police);
    for (int i = 0; i < nGang;     ++i) push(Role::Gang);
    for (int i = 0; i < nHealer;   ++i) push(Role::Healer);
    while ((int)agents.size() < N) push(Role::Civil);
}

bool World::insideBuilding(float x, float y, BType* outType) const {
    for (const auto& b : buildings) {
        if (b.type == BType::Park) continue; // parks are walkable
        // Only the bottom fraction of a building is solid; the top is walkable
        // "behind" space so agents can pass behind tall structures.
        float solidTop = b.pos.y + b.h * (1.0f - kBuildingSolidFrac);
        if (x >= b.pos.x && x <= b.pos.x + b.w &&
            y >= solidTop && y <= b.pos.y + b.h) {
            if (outType) *outType = b.type;
            return true;
        }
    }
    return false;
}

void World::addLog(const std::string& t, SDL_Color c) {
    log.push_back({tick, t, c});
    if (log.size() > 200) log.erase(log.begin(), log.begin() + (log.size() - 200));
}

void World::addFloat(Vec2 p, const std::string& t, SDL_Color c) {
    if (!settings().particles) return;
    if (floats.size() > 70) return;
    floats.push_back({p, t, 1.6f, 1.6f, c});
}

void World::spawnBurst(Vec2 p, SDL_Color c, int n, float speed, bool gravity) {
    if (!settings().particles) return;
    if (particles.size() > 2200) return;
    for (int i = 0; i < n; ++i) {
        Particle pt;
        pt.pos = p;
        float ang = frand(0.0f, 6.2831853f);
        float sp  = frand(speed * 0.3f, speed);
        pt.vel = { std::cos(ang) * sp, std::sin(ang) * sp };
        pt.maxLife = pt.life = frand(0.4f, 1.0f);
        pt.size = frand(1.5f, 3.5f);
        pt.color = c;
        pt.gravity = gravity;
        particles.push_back(pt);
    }
}

int World::pickAgentNear(float wx, float wy, float worldRadius) const {
    int best = -1;
    float bestD2 = worldRadius * worldRadius;
    for (const auto& a : agents) {
        if (!a.alive || a.sleeping) continue;
        float d2 = dist2(a.pos.x, a.pos.y, wx, wy);
        if (d2 < bestD2) { bestD2 = d2; best = a.id; }
    }
    return best;
}

void World::recomputeStats() {
    for (int i = 0; i < (int)Role::COUNT; ++i) stats.count[i] = 0;
    stats.dead = 0;
    double stressSum = 0; int alive = 0;
    for (const auto& a : agents) {
        if (!a.alive) { stats.dead++; continue; }
        stats.count[(int)a.role]++;
        stressSum += a.stress; alive++;
    }
    stats.avgStress = alive ? (float)(stressSum / alive) : 0.0f;
}

// ---------------- Traffic (cosmetic) ----------------
void World::buildRoadNetwork() {
    const auto& s = settings();
    roads.clear();
    float step = roadSpacingForDepth(s.cityDepth);
    // Skip the line at 0 (the world border) for a cleaner frame.
    for (float y = step; y < s.worldH; y += step) roads.push_back({true,  y});
    for (float x = step; x < s.worldW; x += step) roads.push_back({false, x});
}

void World::spawnVehicles(int count) {
    const auto& s = settings();
    vehicles.clear();
    if (roads.empty()) return;
    vehicles.reserve(count);
    for (int i = 0; i < count; ++i) {
        Vehicle v;
        const RoadLine& r = roads[irand(0, (int)roads.size() - 1)];
        v.horizontal = r.horizontal;
        v.axis = r.axis;
        float span = v.horizontal ? s.worldW : s.worldH;
        v.t   = frand(0.0f, span);
        v.dir = irand(0, 1) ? 1.0f : -1.0f;
        v.lane = v.dir * 7.0f;                 // keep to one side (two-way streets)
        v.type = (VehicleType)irand(0, (int)VehicleType::COUNT - 1);
        v.speed = (v.type == VehicleType::Truck) ? frand(50.0f, 72.0f)
                                                 : frand(85.0f, 130.0f);
        vehicles.push_back(v);
    }
}

void World::stepVehicles(float dt) {
    const auto& s = settings();
    for (auto& v : vehicles) {
        float span = v.horizontal ? s.worldW : s.worldH;
        if (span <= 1.0f) continue;
        v.t += v.dir * v.speed * dt;
        while (v.t < 0.0f)    v.t += span;      // wrap around at the world edge
        while (v.t >= span)   v.t -= span;
        if (v.horizontal) v.pos = { v.t,            v.axis + v.lane };
        else              v.pos = { v.axis + v.lane, v.t            };
    }
}

// ---------------- Simulation step ----------------
void World::step(float dt) {
    const auto& s = settings();
    if (dt <= 0) return;
    tick++;

    // Day/night cycle (~120s full loop).
    if (s.dayNight) {
        dayTime += dt / 120.0f;
        if (dayTime >= 1.0f) dayTime -= 1.0f;
    }

    stepVehicles(dt);   // cosmetic traffic moves independently of agents

    grid.build(agents, s.worldW, s.worldH, 90.0f);

    const float hour  = hourOfDay();
    const bool  night = s.dayNight && (hour >= 22.0f || hour < 6.0f);

    const float MAXSPEED = 46.0f;
    const float FEAR_R   = 90.0f;
    const float HUNT_R   = 150.0f;
    const float ACT_R    = 24.0f;

    for (auto& a : agents) {
        if (!a.alive) continue;

        // Sleeping civilians stay hidden inside their home until morning.
        if (a.sleeping) {
            if (night && a.role == Role::Civil) continue; // keep sleeping
            a.sleeping = false;                            // wake at dawn
            if (a.home >= 0 && a.home < (int)buildings.size()) {
                const Building& hb = buildings[a.home];
                a.pos = { hb.pos.x + hb.w * 0.5f, hb.pos.y + hb.h + 14.0f };
                a.pos.x = clampf(a.pos.x, 4.0f, s.worldW - 4.0f);
                a.pos.y = clampf(a.pos.y, 4.0f, s.worldH - 4.0f);
                a.vel = { frand(-10.0f, 10.0f), frand(8.0f, 22.0f) };
            }
        }

        // Decay timers / stress baseline.
        a.actFlash = std::max(0.0f, a.actFlash - dt * 2.0f);
        a.stress   = clampf(a.stress - dt * 0.02f, 0.0f, 1.0f);
        Act nextAct = Act::Walk;

        Vec2 steer{0, 0};
        // Light wander.
        a.vel.x += frand(-40.0f, 40.0f) * dt;
        a.vel.y += frand(-40.0f, 40.0f) * dt;

        switch (a.role) {
        case Role::Civil: {
            // At night, head home to sleep instead of wandering.
            if (night && a.home >= 0 && a.home < (int)buildings.size()) {
                const Building& hb = buildings[a.home];
                Vec2 c{ hb.pos.x + hb.w * 0.5f, hb.pos.y + hb.h * 0.5f };
                steer += (c - a.pos).norm() * 95.0f;
                nextAct = Act::Walk;
                break;
            }
            // Flee from nearby threats.
            int threat = -1; float bd2 = FEAR_R * FEAR_R;
            grid.query(a.pos.x, a.pos.y, FEAR_R, [&](int j) {
                const Agent& o = agents[j];
                if (!o.alive || o.sleeping) return;
                if (o.role == Role::Criminal || o.role == Role::Gang) {
                    float d2 = dist2(a.pos.x, a.pos.y, o.pos.x, o.pos.y);
                    if (d2 < bd2) { bd2 = d2; threat = j; }
                }
            });
            if (threat >= 0) {
                Vec2 away = (a.pos - agents[threat].pos).norm();
                steer += away * 70.0f;
                a.stress = clampf(a.stress + dt * 0.5f, 0.0f, 1.0f);
                nextAct = Act::Flee;
            }
            // High stress can tip a civilian into crime.
            if (a.stress > 0.9f && chance01() < dt * 0.01f) {
                a.role = Role::Criminal; a.stress = 0.5f;
                addFloat(a.pos, "turned!", {232,64,52,255});
            }
            break;
        }
        case Role::Criminal: {
            // Flee police if close, else hunt civilians.
            int cop = -1; float copd2 = (FEAR_R*1.1f)*(FEAR_R*1.1f);
            int victim = -1; float vd2 = HUNT_R * HUNT_R;
            grid.query(a.pos.x, a.pos.y, HUNT_R, [&](int j) {
                const Agent& o = agents[j];
                if (!o.alive || o.sleeping) return;
                float d2 = dist2(a.pos.x, a.pos.y, o.pos.x, o.pos.y);
                if (o.role == Role::Police && d2 < copd2) { copd2 = d2; cop = j; }
                if (o.role == Role::Civil  && d2 < vd2)   { vd2 = d2; victim = j; }
            });
            if (cop >= 0) {
                steer += (a.pos - agents[cop].pos).norm() * 80.0f;
                nextAct = Act::Flee;
            } else if (victim >= 0) {
                Agent& v = agents[victim];
                steer += (v.pos - a.pos).norm() * 60.0f;
                if (vd2 < ACT_R * ACT_R && chance01() < dt * 1.5f && v.money > 1.0f) {
                    float amt = std::min(v.money, frand(5.0f, 35.0f));
                    v.money -= amt; a.money += amt;
                    v.stress = clampf(v.stress + 0.3f, 0, 1);
                    a.actFlash = 1.0f; nextAct = Act::Rob;
                    stats.crimes++; stats.moneyStolen += (long)amt;
                    spawnBurst(v.pos, {255,210,40,255}, 6, 90.0f, true);
                    addFloat(v.pos, "-" + std::to_string((int)amt) + "c", {255,210,40,255});
                }
            }
            break;
        }
        case Role::Police: {
            int target = -1; float td2 = (HUNT_R*1.2f)*(HUNT_R*1.2f);
            grid.query(a.pos.x, a.pos.y, HUNT_R * 1.2f, [&](int j) {
                const Agent& o = agents[j];
                if (!o.alive || o.sleeping) return;
                if (o.role == Role::Criminal || o.role == Role::Gang) {
                    float d2 = dist2(a.pos.x, a.pos.y, o.pos.x, o.pos.y);
                    if (d2 < td2) { td2 = d2; target = j; }
                }
            });
            if (target >= 0) {
                Agent& t = agents[target];
                steer += (t.pos - a.pos).norm() * 85.0f;
                if (td2 < ACT_R * ACT_R && chance01() < dt * 1.2f) {
                    // Arrest: rehabilitate into a civilian.
                    bool wasGang = (t.role == Role::Gang);
                    t.role = Role::Civil; t.stress = 0.3f; t.money = 60.0f;
                    a.actFlash = 1.0f; nextAct = Act::Arrest;
                    stats.arrests++;
                    spawnBurst(t.pos, {64,144,255,255}, 8, 80.0f);
                    addFloat(t.pos, wasGang ? "busted!" : "arrested", {64,144,255,255});
                    addFloat({t.pos.x, t.pos.y - 14.0f},
                             "+$" + std::to_string(econ::kBountyArrest), {255,215,90,255});
                }
            }
            break;
        }
        case Role::Gang: {
            // Fight nearby police, otherwise rob like a criminal.
            int cop = -1; float copd2 = HUNT_R * HUNT_R;
            int victim = -1; float vd2 = HUNT_R * HUNT_R;
            grid.query(a.pos.x, a.pos.y, HUNT_R, [&](int j) {
                const Agent& o = agents[j];
                if (!o.alive || o.sleeping) return;
                float d2 = dist2(a.pos.x, a.pos.y, o.pos.x, o.pos.y);
                if (o.role == Role::Police && d2 < copd2) { copd2 = d2; cop = j; }
                if (o.role == Role::Civil  && d2 < vd2)   { vd2 = d2; victim = j; }
            });
            if (cop >= 0) {
                Agent& c = agents[cop];
                steer += (c.pos - a.pos).norm() * 70.0f;
                if (copd2 < ACT_R * ACT_R && chance01() < dt * 1.0f) {
                    c.health -= frand(0.1f, 0.25f);
                    a.health -= frand(0.05f, 0.15f);
                    a.actFlash = 1.0f; nextAct = Act::Fight;
                    spawnBurst(c.pos, {255,90,40,255}, 5, 70.0f);
                }
            } else if (victim >= 0) {
                Agent& v = agents[victim];
                steer += (v.pos - a.pos).norm() * 55.0f;
                if (vd2 < ACT_R * ACT_R && chance01() < dt * 1.2f && v.money > 1.0f) {
                    float amt = std::min(v.money, frand(10.0f, 45.0f));
                    v.money -= amt; a.money += amt;
                    v.stress = clampf(v.stress + 0.4f, 0, 1);
                    a.actFlash = 1.0f; nextAct = Act::Rob;
                    stats.crimes++; stats.moneyStolen += (long)amt;
                    spawnBurst(v.pos, {255,160,40,255}, 6, 90.0f, true);
                }
            }
            break;
        }
        case Role::Healer: {
            int patient = -1; float pd2 = (HUNT_R*1.3f)*(HUNT_R*1.3f);
            grid.query(a.pos.x, a.pos.y, HUNT_R * 1.3f, [&](int j) {
                const Agent& o = agents[j];
                if (!o.alive || o.sleeping || j == a.id) return;
                if (o.stress > 0.55f || o.health < 0.7f) {
                    float d2 = dist2(a.pos.x, a.pos.y, o.pos.x, o.pos.y);
                    if (d2 < pd2) { pd2 = d2; patient = j; }
                }
            });
            if (patient >= 0) {
                Agent& p = agents[patient];
                steer += (p.pos - a.pos).norm() * 65.0f;
                if (pd2 < ACT_R * ACT_R) {
                    p.stress = clampf(p.stress - dt * 0.8f, 0, 1);
                    p.health = clampf(p.health + dt * 0.5f, 0, 1);
                    a.actFlash = 1.0f; nextAct = Act::Heal;
                    if (chance01() < dt * 1.0f) {
                        stats.heals++;
                        spawnBurst(p.pos, {66,220,120,255}, 4, 50.0f);
                        addFloat(p.pos, "+$" + std::to_string(econ::kBountyHeal) + " heal",
                                 {66,220,120,255});
                    }
                }
            }
            break;
        }
        default: break;
        }

        a.vel += steer * dt * 10.0f;

        // Clamp speed.
        float sp = a.vel.len();
        if (sp > MAXSPEED) a.vel = a.vel * (MAXSPEED / sp);

        // Integrate.
        Vec2 prev = a.pos;
        a.pos += a.vel * dt;

        // Facing + walk animation.
        if (std::fabs(a.vel.x) > 1.0f) a.facing = a.vel.x > 0 ? 1.0f : -1.0f;
        a.animPhase += (sp * 0.06f + 2.0f) * dt;
        a.act = (sp > 4.0f || nextAct != Act::Walk) ? nextAct : Act::Idle;

        // World bounds.
        if (a.pos.x < 0)        { a.pos.x = 0;        a.vel.x *= -0.5f; }
        if (a.pos.y < 0)        { a.pos.y = 0;        a.vel.y *= -0.5f; }
        if (a.pos.x > s.worldW) { a.pos.x = s.worldW; a.vel.x *= -0.5f; }
        if (a.pos.y > s.worldH) { a.pos.y = s.worldH; a.vel.y *= -0.5f; }

        // Building collision: push out of solid buildings, reduce damage stat.
        BType bt;
        if (insideBuilding(a.pos.x, a.pos.y, &bt)) {
            // Revert to previous position (simple, robust) and nudge.
            a.pos = prev;
            a.vel = a.vel * -0.4f;
        }

        // Stress reduction in parks.
        for (const auto& b : buildings) {
            if (b.type != BType::Park) continue;
            if (a.pos.x >= b.pos.x && a.pos.x <= b.pos.x + b.w &&
                a.pos.y >= b.pos.y && a.pos.y <= b.pos.y + b.h) {
                a.stress = clampf(a.stress - dt * 0.25f, 0, 1);
                break;
            }
        }

        // Reached home at night => slip inside and fall asleep (hidden).
        if (a.role == Role::Civil && night && a.home >= 0 && a.home < (int)buildings.size()) {
            const Building& hb = buildings[a.home];
            float nx = clampf(a.pos.x, hb.pos.x, hb.pos.x + hb.w);
            float ny = clampf(a.pos.y, hb.pos.y, hb.pos.y + hb.h);
            if (dist2(a.pos.x, a.pos.y, nx, ny) < 20.0f * 20.0f) {
                a.sleeping = true;
                a.pos = { hb.pos.x + hb.w * 0.5f, hb.pos.y + hb.h * 0.5f };
                a.vel = { 0, 0 };
                a.act = Act::Idle;
            }
        }

        // Death from low health or rare natural causes.
        if (a.health <= 0.0f) {
            a.alive = false;
            spawnBurst(a.pos, {120,120,130,255}, 10, 60.0f);
            addLog(std::string(roleName(a.role)) + " #" + std::to_string(a.id) + " died in a fight.",
                   {200,80,80,255});
        } else if (chance01() < dt * 0.0008f) {
            a.alive = false;
            spawnBurst(a.pos, {120,120,130,255}, 6, 40.0f);
        }
    }

    // Update particles.
    for (auto& p : particles) {
        p.life -= dt;
        if (p.gravity) p.vel.y += 140.0f * dt;
        p.pos += p.vel * dt;
        p.vel = p.vel * (1.0f - 0.8f * dt);
    }
    particles.erase(std::remove_if(particles.begin(), particles.end(),
        [](const Particle& p) { return p.life <= 0; }), particles.end());

    // Update floating texts.
    for (auto& f : floats) { f.life -= dt; f.pos.y -= 18.0f * dt; }
    floats.erase(std::remove_if(floats.begin(), floats.end(),
        [](const FloatText& f) { return f.life <= 0; }), floats.end());

    recomputeStats();
}

} // namespace cv
