// OmniVerse — LogOS Engine (SDL2 redesign)
// Entry point. All gameplay lives in the Game class.
#include "Game.h"
#include <iostream>
#include <cstring>

int main(int argc, char** argv) {
    // Headless screenshot mode:  OmniVerse --shot out.bmp [frames]
    if (argc >= 3 && std::strcmp(argv[1], "--shot") == 0) {
        cv::Game game;
        if (!game.initHeadless()) return 1;
        int frames = (argc >= 4) ? std::atoi(argv[3]) : 180;
        game.captureFrames(argv[2], frames);
        game.shutdown();
        return 0;
    }

    cv::Game game;
    if (!game.init()) {
        std::cerr << "Failed to initialize OmniVerse.\n";
        return 1;
    }
    game.run();
    game.shutdown();
    return 0;
}
