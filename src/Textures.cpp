#include "Textures.h"

#include <vector>
#include <cstring>

// Vendored PNG decoder. We only need PNG and we feed it memory buffers (the
// bytes come through SDL_RWops so it works inside an Android APK too), so trim
// the build to just what we use. Trimming leaves a couple of helpers unused, so
// quiet that one warning locally around the implementation include.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "stb_image.h"
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

namespace cv {

// Open an asset by bare file name across platforms. On Android SDL maps
// SDL_RWFromFile to the APK's AAssetManager (root = the assets/ folder), so the
// name is used as-is. On desktop we look next to the executable first
// (<basePath>/assets/), then the current working directory (./assets/).
static SDL_RWops* openAsset(const std::string& name) {
#ifdef __ANDROID__
    return SDL_RWFromFile(name.c_str(), "rb");
#else
    static std::string base;
    static bool gotBase = false;
    if (!gotBase) {
        char* b = SDL_GetBasePath();
        base = b ? b : "";
        SDL_free(b);
        gotBase = true;
    }
    if (!base.empty()) {
        std::string p = base + "assets/" + name;
        if (SDL_RWops* rw = SDL_RWFromFile(p.c_str(), "rb")) return rw;
    }
    std::string cwd = "assets/" + name;
    return SDL_RWFromFile(cwd.c_str(), "rb");
#endif
}

// Read a whole asset into memory (works for APK assets, which are not seekable
// as plain files). Returns empty on failure.
static std::vector<unsigned char> readAsset(const std::string& name) {
    SDL_RWops* rw = openAsset(name);
    if (!rw) return {};
    Sint64 size = SDL_RWsize(rw);
    std::vector<unsigned char> buf;
    if (size > 0) {
        buf.resize((size_t)size);
        size_t got = SDL_RWread(rw, buf.data(), 1, (size_t)size);
        buf.resize(got);
    } else {
        // Unknown size (some streams): read in chunks.
        unsigned char tmp[4096];
        size_t got;
        while ((got = SDL_RWread(rw, tmp, 1, sizeof(tmp))) > 0)
            buf.insert(buf.end(), tmp, tmp + got);
    }
    SDL_RWclose(rw);
    return buf;
}

static bool endsWith(const std::string& s, const char* suf) {
    size_t n = std::strlen(suf);
    return s.size() >= n && SDL_strncasecmp(s.c_str() + (s.size() - n), suf, n) == 0;
}

SDL_Texture* Textures::load(const std::string& file) {
    if (!ren_) return nullptr;

    std::vector<unsigned char> bytes = readAsset(file);
    if (bytes.empty()) {
        SDL_Log("Textures: asset not found / empty: %s", file.c_str());
        return nullptr;
    }

    SDL_Surface* surf = nullptr;
    bool freeStbi = false;
    unsigned char* stbiPixels = nullptr;

    if (endsWith(file, ".bmp")) {
        // BMP: SDL core can decode it. The supplied BMPs are 24-bit with a pure
        // black background, so key out black to get transparency.
        SDL_RWops* mem = SDL_RWFromConstMem(bytes.data(), (int)bytes.size());
        if (mem) {
            surf = SDL_LoadBMP_RW(mem, 1 /*free mem*/);
            if (surf) SDL_SetColorKey(surf, SDL_TRUE,
                                      SDL_MapRGB(surf->format, 0, 0, 0));
        }
    } else {
        // PNG (and anything else) via stb_image -> tightly-packed RGBA8888.
        int w = 0, h = 0, comp = 0;
        stbiPixels = stbi_load_from_memory(bytes.data(), (int)bytes.size(),
                                           &w, &h, &comp, 4);
        if (stbiPixels) {
            freeStbi = true;
            surf = SDL_CreateRGBSurfaceWithFormatFrom(
                stbiPixels, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
        } else {
            SDL_Log("Textures: stbi decode failed for %s: %s",
                    file.c_str(), stbi_failure_reason());
        }
    }

    if (!surf) {
        if (freeStbi) stbi_image_free(stbiPixels);
        return nullptr;
    }

    SDL_Texture* tex = SDL_CreateTextureFromSurface(ren_, surf);
    SDL_FreeSurface(surf);
    if (freeStbi) stbi_image_free(stbiPixels);   // surface copied the pixels

    if (!tex) {
        SDL_Log("Textures: CreateTexture failed for %s: %s",
                file.c_str(), SDL_GetError());
        return nullptr;
    }
    // Crisp pixel-art scaling and straight-alpha blending.
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    return tex;
}

void Textures::loadAll() {
    if (!ren_) return;

    // Nearest-neighbor keeps the pixel art sharp when scaled up.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    static const char* kAssets[] = {
        // Buildings — day/night × lights on/off.
        "ApartmentDayLightON.png",  "ApartmentDayLightOFF.png",
        "ApartmentNightLightON.png","ApartmentNightLightOFF.png",
        "HouseDayLightON.png",      "HouseDayLightOFF.png",
        "HouseNightLightON.png",    "HouseNightLightOFF.png",
        "JobDayLightON.png",        "JobDayLightOFF.png",
        "JobNightLightON.png",      "JobNightLightOFF.png",
        // Roads.
        "AsphaltLinear.png",  "AsphaltParralel.png",
        "AsphaltCorner.png",  "AsphaltCornerDown.png",
        "AsphaltCornerUp.png","AsphaltCornerStraight.png",
        // Vehicles (loaded and ready; the agent layer is unchanged for now).
        "CivilianCar.png", "LuxuryCar.png", "Truck.png",
        // BMP sprites.
        "factory.bmp", "house.bmp", "shop.bmp", "tree.bmp",
    };

    for (const char* name : kAssets) {
        if (cache_.count(name)) continue;
        SDL_Texture* t = load(name);
        cache_[name] = t;            // store even nullptr so we don't retry
        if (t) anyLoaded_ = true;
    }
    SDL_Log("Textures: loaded sprite set (%s)",
            anyLoaded_ ? "ok" : "none found — using procedural fallback");
}

SDL_Texture* Textures::get(const std::string& name) const {
    auto it = cache_.find(name);
    return it == cache_.end() ? nullptr : it->second;
}

void Textures::shutdown() {
    for (auto& kv : cache_)
        if (kv.second) SDL_DestroyTexture(kv.second);
    cache_.clear();
    anyLoaded_ = false;
}

} // namespace cv
