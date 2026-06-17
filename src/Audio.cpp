#include "Audio.h"
#include "Config.h"
#include "Math.h"
#include <vector>
#include <cmath>

namespace cv {

bool Audio::init() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { ok_ = false; return false; }
    SDL_AudioSpec want{}, have{};
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = nullptr; // we use SDL_QueueAudio
    dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (dev_ == 0) { ok_ = false; return false; }
    freq_ = have.freq;
    SDL_PauseAudioDevice(dev_, 0);
    ok_ = true;
    return true;
}

void Audio::shutdown() {
    if (dev_) { SDL_CloseAudioDevice(dev_); dev_ = 0; }
    ok_ = false;
}

void Audio::blip(float freqHz, float durSec, float vol, int wave) {
    if (!ok_ || !settings().sound) return;
    vol *= clampf(settings().volume, 0.0f, 1.0f);
    if (vol <= 0.0001f) return;

    int n = std::max(1, (int)(durSec * freq_));
    std::vector<Sint16> buf(n);
    for (int i = 0; i < n; ++i) {
        float t = (float)i / (float)freq_;
        float ph = std::fmod(freqHz * t, 1.0f); // 0..1 phase
        float s;
        switch (wave) {
            case 1:  s = ph < 0.5f ? 1.0f : -1.0f; break;             // square
            case 2:  s = 4.0f * std::fabs(ph - 0.5f) - 1.0f; break;   // triangle
            default: s = std::sin(ph * 6.2831853f); break;            // sine
        }
        // Simple attack/release envelope to avoid clicks.
        float env = 1.0f;
        float att = 0.01f, rel = 0.04f;
        if (t < att)            env = t / att;
        else if (t > durSec - rel) env = std::max(0.0f, (durSec - t) / rel);
        float v = s * env * vol;
        buf[i] = (Sint16)(clampf(v, -1.0f, 1.0f) * 28000.0f);
    }
    SDL_QueueAudio(dev_, buf.data(), (Uint32)(buf.size() * sizeof(Sint16)));
}

} // namespace cv
