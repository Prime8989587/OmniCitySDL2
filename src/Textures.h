// Textures.h — sprite asset loader/cache for the player-supplied PNG/BMP art.
//
// PNG is decoded with the vendored stb_image (single header, public domain) so
// there is NO SDL_image dependency. BMP is loaded with SDL core and color-keyed
// on pure black. Assets resolve from the APK's assets/ root on Android and from
// <basePath>/assets (or ./assets) on desktop. Every accessor is null-safe: a
// missing/failed asset simply returns nullptr and the caller falls back to the
// original procedural drawing, so the game never crashes on absent art.
#pragma once
#include <SDL.h>
#include <string>
#include <unordered_map>

namespace cv {

class Textures {
public:
    void init(SDL_Renderer* r) { ren_ = r; }
    void loadAll();          // decode + upload the known asset set (idempotent)
    void shutdown();         // destroy every cached texture

    // Cached texture for an asset file name (e.g. "JobDayLightON.png"), or
    // nullptr if it is missing or failed to decode. Cheap O(1) lookup.
    SDL_Texture* get(const std::string& name) const;

    // True once at least one asset decoded — lets renderers switch to sprites.
    bool ready() const { return anyLoaded_; }

private:
    SDL_Texture* load(const std::string& file);   // decode one file, cache it
    SDL_Renderer* ren_ = nullptr;
    std::unordered_map<std::string, SDL_Texture*> cache_;
    bool anyLoaded_ = false;
};

} // namespace cv
