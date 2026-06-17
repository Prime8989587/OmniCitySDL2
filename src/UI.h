// UI.h — lightweight immediate-mode widgets built on the draw + font modules.
#pragma once
#include <SDL.h>
#include <string>
#include "Math.h"

namespace cv {

struct InputState {
    int  mouseX = 0, mouseY = 0;
    bool mouseDown = false;      // left button currently held
    bool mousePressed = false;   // left button went down this frame
    bool mouseReleased = false;  // left button went up this frame
    int  wheel = 0;              // wheel delta this frame

    bool hit(const SDL_Rect& r) const {
        return mouseX >= r.x && mouseX < r.x + r.w &&
               mouseY >= r.y && mouseY < r.y + r.h;
    }
};

namespace ui {

// Themed colors.
inline SDL_Color panelBg()    { return {18, 22, 34, 235}; }
inline SDL_Color panelEdge()  { return {60, 80, 120, 255}; }
inline SDL_Color accent()     { return {80, 170, 255, 255}; }
inline SDL_Color textMain()   { return {225, 232, 245, 255}; }
inline SDL_Color textDim()    { return {140, 152, 172, 255}; }

void panel(SDL_Renderer* r, const SDL_Rect& rc);
void panel(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color bg, SDL_Color edge);

// Returns true on click (released inside after press inside).
bool button(SDL_Renderer* r, const SDL_Rect& rc, const std::string& label,
            const InputState& in, SDL_Color accentCol, int textScale = 2);

// Toggle button; flips `value` when clicked, returns true if changed.
bool toggle(SDL_Renderer* r, const SDL_Rect& rc, const std::string& label,
            bool& value, const InputState& in, int textScale = 2);

// Horizontal slider; edits `value` in [minV,maxV], returns true if changed.
bool sliderF(SDL_Renderer* r, const SDL_Rect& rc, const std::string& label,
             float& value, float minV, float maxV, const InputState& in);

} // namespace ui
} // namespace cv
