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

void World::generateBuildings() {
    const auto& s = settings();
    buildings.clear();
    int n = std::max(4, s.numBuildings);
    // Guarantee at least one police station and one hospital.
    for (int i = 0; i < n; ++i) {
        Building b;
        b.w = frand(90.0f, 240.0f);
        b.h = frand(90.0f, 240.0f);
        b.pos.x = frand(160.0f, s.worldW - 160.0f - b.w);
        b.pos.y = frand(160.0f, s.worldH - 160.0f - b.h);
        if (i == 0)      b.type = BType::PoliceStation;
        else if (i == 1) b.type = BType::Hospital;
        else             b.type = (BType)irand(0, (int)BType::Park); // res/office/industry/park
        b.variant = irand(0, 3);
        b.windowSeed = (unsigned)irand(1, 1 << 30);
        buildings.push_back(b);
    }
}

void World::generateTrees() {
    const auto& s = settings();
    trees.clear();
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
        a.pos = { frand(80.0f, s.worldW - 80.0f), frand(80.0f, s.worldH - 80.0f) };
        a.vel = { frand(-15.0f, 15.0f), frand(-15.0f, 15.0f) };
        a.role = r;
        a.stress = frand(0.05f, 0.35f);
        a.money  = frand(40.0f, 160.0f);
        a.animPhase = frand(0.0f, 6.28f);
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
