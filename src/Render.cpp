#include "Render.h"

namespace cv {

void Camera::update(float dt) {
    // Critically-damped-ish smoothing, frame-rate aware.
    float k = clampf(dt * 9.0f, 0.0f, 1.0f);
    x    = lerp(x, tx, k);
    y    = lerp(y, ty, k);
    zoom = lerp(zoom, tzoom, k);
}

void View::worldToScreen(float wx, float wy, int& sx, int& sy) const {
    float dx = (wx - cam.x) * cam.zoom;
    float dy = (wy - cam.y) * cam.zoom;
    sx = (int)std::lround(viewX + viewW * 0.5f + dx);
    sy = (int)std::lround(viewY + viewH * 0.5f + dy);
}

void View::screenToWorld(int sx, int sy, float& wx, float& wy) const {
    float dx = (sx - (viewX + viewW * 0.5f)) / cam.zoom;
    float dy = (sy - (viewY + viewH * 0.5f)) / cam.zoom;
    wx = cam.x + dx;
    wy = cam.y + dy;
}

bool View::worldRectToScreen(float wx, float wy, float ww, float wh, SDL_Rect& out) const {
    int sx1, sy1, sx2, sy2;
    worldToScreen(wx, wy, sx1, sy1);
    worldToScreen(wx + ww, wy + wh, sx2, sy2);
    int x = std::min(sx1, sx2), y = std::min(sy1, sy2);
    int w = std::abs(sx2 - sx1), h = std::abs(sy2 - sy1);
    if (x > viewX + viewW || y > viewY + viewH || x + w < viewX || y + h < viewY)
        return false;
    out = {x, y, w, h};
    return true;
}

namespace draw {

void fillRect(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r, &rc);
}
void rect(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderDrawRect(r, &rc);
}
void line(SDL_Renderer* r, int x1, int y1, int x2, int y2, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderDrawLine(r, x1, y1, x2, y2);
}
void thickLine(SDL_Renderer* r, int x1, int y1, int x2, int y2, int w, SDL_Color c) {
    // Simple thick line via perpendicular offset rects/lines.
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    for (int i = -w / 2; i <= w / 2; ++i) {
        SDL_RenderDrawLine(r, x1, y1 + i, x2, y2 + i);
        SDL_RenderDrawLine(r, x1 + i, y1, x2 + i, y2);
    }
}

void roundedRect(SDL_Renderer* r, SDL_Rect rc, int radius, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    radius = std::min(radius, std::min(rc.w, rc.h) / 2);
    if (radius <= 0) { SDL_RenderFillRect(r, &rc); return; }
    // Center band
    SDL_Rect mid{ rc.x, rc.y + radius, rc.w, rc.h - 2 * radius };
    SDL_RenderFillRect(r, &mid);
    // Top & bottom bands with rounded corners drawn as horizontal spans.
    for (int dy = 0; dy < radius; ++dy) {
        int dx = (int)std::lround(std::sqrt((double)radius * radius - (radius - dy) * (radius - dy)));
        // top
        SDL_RenderDrawLine(r, rc.x + radius - dx, rc.y + dy,
                              rc.x + rc.w - radius + dx - 1, rc.y + dy);
        // bottom
        SDL_RenderDrawLine(r, rc.x + radius - dx, rc.y + rc.h - 1 - dy,
                              rc.x + rc.w - radius + dx - 1, rc.y + rc.h - 1 - dy);
    }
}

void roundedRectOutline(SDL_Renderer* r, SDL_Rect rc, int radius, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    radius = std::min(radius, std::min(rc.w, rc.h) / 2);
    if (radius <= 0) { SDL_RenderDrawRect(r, &rc); return; }
    // Straight edges
    SDL_RenderDrawLine(r, rc.x + radius, rc.y, rc.x + rc.w - radius - 1, rc.y);
    SDL_RenderDrawLine(r, rc.x + radius, rc.y + rc.h - 1, rc.x + rc.w - radius - 1, rc.y + rc.h - 1);
    SDL_RenderDrawLine(r, rc.x, rc.y + radius, rc.x, rc.y + rc.h - radius - 1);
    SDL_RenderDrawLine(r, rc.x + rc.w - 1, rc.y + radius, rc.x + rc.w - 1, rc.y + rc.h - radius - 1);
    // Corner arcs (midpoint-ish)
    int x = radius, y = 0, err = 1 - radius;
    while (x >= y) {
        auto plot = [&](int ox, int oy) { SDL_RenderDrawPoint(r, ox, oy); };
        int cxL = rc.x + radius, cxR = rc.x + rc.w - radius - 1;
        int cyT = rc.y + radius, cyB = rc.y + rc.h - radius - 1;
        plot(cxR + x, cyB + y); plot(cxR + y, cyB + x);
        plot(cxL - x, cyB + y); plot(cxL - y, cyB + x);
        plot(cxR + x, cyT - y); plot(cxR + y, cyT - x);
        plot(cxL - x, cyT - y); plot(cxL - y, cyT - x);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void fillCircle(SDL_Renderer* r, int cx, int cy, int radius, SDL_Color c) {
    if (radius <= 0) return;
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    for (int dy = -radius; dy <= radius; ++dy) {
        int dx = (int)std::lround(std::sqrt((double)radius * radius - dy * dy));
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

void circleOutline(SDL_Renderer* r, int cx, int cy, int radius, SDL_Color c) {
    if (radius <= 0) return;
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    int x = radius, y = 0, err = 1 - radius;
    while (x >= y) {
        SDL_RenderDrawPoint(r, cx + x, cy + y); SDL_RenderDrawPoint(r, cx + y, cy + x);
        SDL_RenderDrawPoint(r, cx - x, cy + y); SDL_RenderDrawPoint(r, cx - y, cy + x);
        SDL_RenderDrawPoint(r, cx - x, cy - y); SDL_RenderDrawPoint(r, cx - y, cy - x);
        SDL_RenderDrawPoint(r, cx + x, cy - y); SDL_RenderDrawPoint(r, cx + y, cy - x);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void fillEllipse(SDL_Renderer* r, int cx, int cy, int rx, int ry, SDL_Color c) {
    if (rx <= 0 || ry <= 0) return;
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    for (int dy = -ry; dy <= ry; ++dy) {
        double f = 1.0 - (double)(dy * dy) / (double)(ry * ry);
        if (f < 0) continue;
        int dx = (int)std::lround(rx * std::sqrt(f));
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

void fillTriangleUp(SDL_Renderer* r, int cx, int apexY, int halfW, int height, SDL_Color c) {
    if (height <= 0) return;
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    for (int dy = 0; dy <= height; ++dy) {
        int hw = (int)std::lround((double)halfW * dy / height);
        SDL_RenderDrawLine(r, cx - hw, apexY + dy, cx + hw, apexY + dy);
    }
}

void fillTrapezoid(SDL_Renderer* r, int xTopL, int xTopR, int yTop,
                   int xBotL, int xBotR, int yBot, SDL_Color c) {
    if (yBot < yTop) { std::swap(yTop, yBot); std::swap(xTopL, xBotL); std::swap(xTopR, xBotR); }
    int span = std::max(1, yBot - yTop);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    for (int y = yTop; y <= yBot; ++y) {
        float t = (float)(y - yTop) / (float)span;
        int xl = (int)std::lround(xTopL + (xBotL - xTopL) * t);
        int xr = (int)std::lround(xTopR + (xBotR - xTopR) * t);
        SDL_RenderDrawLine(r, xl, y, xr, y);
    }
}

void vGradient(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color top, SDL_Color bottom) {
    if (rc.h <= 0) return;
    for (int i = 0; i < rc.h; ++i) {
        float t = (float)i / (float)(rc.h - 1 > 0 ? rc.h - 1 : 1);
        SDL_Color c = lerpColor(top, bottom, t);
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_RenderDrawLine(r, rc.x, rc.y + i, rc.x + rc.w - 1, rc.y + i);
    }
}

} // namespace draw
} // namespace cv
