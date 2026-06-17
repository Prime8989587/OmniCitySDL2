// Math.h — small vector math, RNG helpers, easing, color utilities.
#pragma once
#include <cmath>
#include <random>
#include <chrono>
#include <algorithm>
#include <cstdint>
#include <SDL2/SDL.h>

namespace cv {

// ---------------- RNG ----------------
inline std::mt19937& rng() {
    static std::mt19937 g(
        (unsigned)std::chrono::high_resolution_clock::now().time_since_epoch().count());
    return g;
}
inline float frand(float a, float b) {
    std::uniform_real_distribution<float> d(a, b); return d(rng());
}
inline int irand(int a, int b) {
    std::uniform_int_distribution<int> d(a, b); return d(rng());
}
inline float chance01() { return frand(0.0f, 1.0f); }

// ---------------- Vec2 ----------------
struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    float len()  const { return std::sqrt(x * x + y * y); }
    float len2() const { return x * x + y * y; }
    Vec2 norm() const { float l = len(); return l > 1e-6f ? Vec2{x / l, y / l} : Vec2{0, 0}; }
};

inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float dist2(float ax, float ay, float bx, float by) {
    float dx = ax - bx, dy = ay - by; return dx * dx + dy * dy;
}

// Smooth approach (frame-rate independent-ish), factor in [0,1].
inline float approach(float cur, float target, float factor) {
    return cur + (target - cur) * clampf(factor, 0.0f, 1.0f);
}

// ---------------- Easing ----------------
inline float easeOutCubic(float t) { float u = 1 - t; return 1 - u * u * u; }
inline float easeInOutQuad(float t) {
    return t < 0.5f ? 2 * t * t : 1 - std::pow(-2 * t + 2, 2) / 2;
}

// ---------------- Color ----------------
inline SDL_Color rgb(Uint8 r, Uint8 g, Uint8 b, Uint8 a = 255) { return {r, g, b, a}; }

inline SDL_Color lerpColor(SDL_Color a, SDL_Color b, float t) {
    t = clampf(t, 0.0f, 1.0f);
    return {
        (Uint8)lerp(a.r, b.r, t),
        (Uint8)lerp(a.g, b.g, t),
        (Uint8)lerp(a.b, b.b, t),
        (Uint8)lerp(a.a, b.a, t)
    };
}

inline SDL_Color scaleColor(SDL_Color c, float s) {
    return {
        (Uint8)clampf(c.r * s, 0, 255),
        (Uint8)clampf(c.g * s, 0, 255),
        (Uint8)clampf(c.b * s, 0, 255),
        c.a
    };
}

} // namespace cv
