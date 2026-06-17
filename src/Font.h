// Font.h — built-in 5x7 bitmap font (no SDL_ttf dependency).
// Renders crisp scaled text using filled rectangles. All-caps glyph set;
// lowercase letters are mapped to uppercase glyphs.
#pragma once
#include <SDL.h>
#include <string>

namespace cv {

enum class Align { Left, Center, Right };

namespace font {

// Glyph cell metrics at scale 1 (in pixels).
constexpr int GLYPH_W = 5;
constexpr int GLYPH_H = 7;

// Pixel width/height of a string at a given scale (1px gap between glyphs).
int   textWidth(const std::string& s, int scale);
inline int textHeight(int scale) { return GLYPH_H * scale; }

// Draw text. (x,y) is the top-left of the text block (before alignment).
void draw(SDL_Renderer* r, const std::string& s, int x, int y,
          int scale, SDL_Color color, Align align = Align::Left);

// Draw text with a 1px drop shadow for readability over busy backgrounds.
void drawShadowed(SDL_Renderer* r, const std::string& s, int x, int y,
                  int scale, SDL_Color color, Align align = Align::Left);

} // namespace font
} // namespace cv
