// Render.h — camera, world<->screen transforms, and low-level draw primitives.
#pragma once
#include <SDL.h>
#include "Math.h"

namespace cv {

struct Camera {
    float x = 0, y = 0;     // world-space center the camera looks at
    float zoom = 0.6f;      // 1.0 => 1 world unit == 1 pixel
    // Smoothed targets for nice motion.
    float tx = 0, ty = 0, tzoom = 0.6f;

    void snap(float wx, float wy, float z) { x = tx = wx; y = ty = wy; zoom = tzoom = z; }
    void update(float dt);  // ease toward targets
};

// Transform helpers (need screen size, so pass it in).
struct View {
    Camera cam;
    int screenW, screenH;
    // Visible region in screen used by the world (sidebar may shrink it).
    int viewX = 0, viewY = 0, viewW = 0, viewH = 0;

    void worldToScreen(float wx, float wy, int& sx, int& sy) const;
    void screenToWorld(int sx, int sy, float& wx, float& wy) const;
    bool worldRectToScreen(float wx, float wy, float ww, float wh, SDL_Rect& out) const;
};

namespace draw {

void fillRect(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c);
void rect(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c);       // outline
void roundedRect(SDL_Renderer* r, SDL_Rect rc, int radius, SDL_Color c);
void roundedRectOutline(SDL_Renderer* r, SDL_Rect rc, int radius, SDL_Color c);
void fillCircle(SDL_Renderer* r, int cx, int cy, int radius, SDL_Color c);
void circleOutline(SDL_Renderer* r, int cx, int cy, int radius, SDL_Color c);
// Upward isosceles triangle: apex at (cx, apexY), base of width 2*halfW at apexY+height.
void fillTriangleUp(SDL_Renderer* r, int cx, int apexY, int halfW, int height, SDL_Color c);
void line(SDL_Renderer* r, int x1, int y1, int x2, int y2, SDL_Color c);
void thickLine(SDL_Renderer* r, int x1, int y1, int x2, int y2, int w, SDL_Color c);
// Vertical gradient fill across a rect.
void vGradient(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color top, SDL_Color bottom);

} // namespace draw
} // namespace cv
