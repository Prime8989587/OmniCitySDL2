#include "UI.h"
#include "Render.h"
#include "Font.h"

namespace cv {
namespace ui {

void panel(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color bg, SDL_Color edge) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    draw::fillRect(r, rc, bg);
    draw::thickRect(r, rc, 2, edge);
}
void panel(SDL_Renderer* r, const SDL_Rect& rc) { panel(r, rc, panelBg(), panelEdge()); }

bool button(SDL_Renderer* r, const SDL_Rect& rc, const std::string& label,
            const InputState& in, SDL_Color accentCol, int textScale) {
    bool hov = in.hit(rc);
    bool held = hov && in.mouseDown;
    SDL_Color bg = held ? scaleColor(accentCol, 0.55f)
                  : hov  ? scaleColor(accentCol, 0.40f)
                         : SDL_Color{34, 42, 60, 255};
    SDL_Color edge = hov ? accentCol : SDL_Color{70, 86, 120, 255};
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    draw::fillRect(r, rc, bg);
    draw::thickRect(r, rc, hov ? 2 : 1, edge);
    SDL_Color tc = hov ? SDL_Color{255,255,255,255} : textMain();
    font::draw(r, label, rc.x + rc.w / 2, rc.y + (rc.h - font::textHeight(textScale)) / 2,
               textScale, tc, Align::Center);
    return hov && in.mouseReleased;
}

bool toggle(SDL_Renderer* r, const SDL_Rect& rc, const std::string& label,
            bool& value, const InputState& in, int textScale) {
    bool hov = in.hit(rc);
    SDL_Color on = {66, 200, 120, 255};
    SDL_Color off = {120, 70, 70, 255};
    SDL_Color bg = value ? scaleColor(on, hov ? 0.7f : 0.55f)
                         : scaleColor(off, hov ? 0.7f : 0.55f);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    draw::fillRect(r, rc, bg);
    draw::thickRect(r, rc, hov ? 2 : 1, hov ? accent() : panelEdge());
    std::string txt = label + (value ? ": ON" : ": OFF");
    font::draw(r, txt, rc.x + rc.w / 2, rc.y + (rc.h - font::textHeight(textScale)) / 2,
               textScale, textMain(), Align::Center);
    if (hov && in.mouseReleased) { value = !value; return true; }
    return false;
}

bool sliderF(SDL_Renderer* r, const SDL_Rect& rc, const std::string& label,
             float& value, float minV, float maxV, const InputState& in) {
    // Label above the track.
    font::draw(r, label, rc.x, rc.y - font::textHeight(1) - 4, 1, textDim(), Align::Left);
    SDL_Rect track{ rc.x, rc.y + rc.h / 2 - 3, rc.w, 6 };
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    draw::fillRect(r, track, {40, 48, 66, 255});
    float t = (value - minV) / (maxV - minV);
    t = clampf(t, 0.0f, 1.0f);
    SDL_Rect fill{ rc.x, track.y, (int)(rc.w * t), track.h };
    draw::fillRect(r, fill, accent());
    int knobX = rc.x + (int)(rc.w * t);
    bool hov = in.hit(rc);
    SDL_Rect knob{ knobX - 5, rc.y + rc.h / 2 - 6, 10, 12 };
    draw::fillRect(r, knob, hov ? SDL_Color{255,255,255,255} : SDL_Color{200,210,230,255});
    draw::rect(r, knob, {30, 36, 50, 255});

    bool changed = false;
    if (hov && in.mouseDown) {
        float nt = clampf((float)(in.mouseX - rc.x) / (float)rc.w, 0.0f, 1.0f);
        float nv = minV + nt * (maxV - minV);
        if (nv != value) { value = nv; changed = true; }
    }
    // Numeric readout.
    char buf[32]; std::snprintf(buf, sizeof(buf), "%.2f", value);
    font::draw(r, buf, rc.x + rc.w, rc.y - font::textHeight(1) - 4, 1, textMain(), Align::Right);
    return changed;
}

} // namespace ui
} // namespace cv
