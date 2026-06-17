// Audio.h — tiny procedural sound engine (synthesized tones, no asset files).
// Fails gracefully when no audio device is available (e.g. headless).
#pragma once
#include <SDL.h>

namespace cv {

class Audio {
public:
    bool init();
    void shutdown();
    bool ok() const { return ok_; }

    // Queue a short synthesized tone. wave: 0=sine, 1=square, 2=triangle.
    void blip(float freqHz, float durSec, float vol, int wave = 0);

    // Convenience SFX.
    void click()  { blip(660.0f, 0.05f, 0.4f, 1); }
    void select() { blip(880.0f, 0.06f, 0.4f, 0); }
    void crime()  { blip(180.0f, 0.10f, 0.5f, 1); }
    void arrest() { blip(520.0f, 0.08f, 0.45f, 2); blip(780.0f, 0.06f, 0.4f, 2); }
    void deploy() { blip(440.0f, 0.07f, 0.45f, 2); blip(660.0f, 0.07f, 0.45f, 2); }
    void alarm()  { blip(300.0f, 0.18f, 0.5f, 1); }
    void win()    { blip(523, 0.12f, 0.5f, 2); blip(659, 0.12f, 0.5f, 2); blip(784, 0.18f, 0.5f, 2); }
    void lose()   { blip(330, 0.16f, 0.5f, 1); blip(247, 0.16f, 0.5f, 1); blip(165, 0.24f, 0.5f, 1); }

private:
    SDL_AudioDeviceID dev_ = 0;
    int  freq_ = 44100;
    bool ok_ = false;
};

} // namespace cv
