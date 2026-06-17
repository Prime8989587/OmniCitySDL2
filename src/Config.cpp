#include "Config.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace cv {

Settings& settings() {
    static Settings s;
    return s;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static bool toBool(const std::string& v) {
    std::string t = v;
    std::transform(t.begin(), t.end(), t.begin(), ::tolower);
    return t == "1" || t == "true" || t == "yes" || t == "on";
}

void Settings::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        if (key.empty()) continue;

        try {
            if      (key == "screenW")      screenW      = std::stoi(val);
            else if (key == "screenH")      screenH      = std::stoi(val);
            else if (key == "fullscreen")   fullscreen   = toBool(val);
            else if (key == "vsync")        vsync        = toBool(val);
            else if (key == "worldW")       worldW       = std::stof(val);
            else if (key == "worldH")       worldH       = std::stof(val);
            else if (key == "numAgents")    numAgents    = std::stoi(val);
            else if (key == "numBuildings") numBuildings = std::stoi(val);
            else if (key == "numTrees")     numTrees     = std::stoi(val);
            else if (key == "animations")   animations   = toBool(val);
            else if (key == "shadows")      shadows      = toBool(val);
            else if (key == "dayNight")     dayNight     = toBool(val);
            else if (key == "particles")    particles    = toBool(val);
            else if (key == "grass")        grass        = toBool(val);
            else if (key == "water")        water        = toBool(val);
            else if (key == "flowers")      flowers      = toBool(val);
            else if (key == "sound")        sound        = toBool(val);
            else if (key == "volume")       volume       = std::stof(val);
        } catch (...) {
            // ignore malformed values, keep defaults
        }
    }
}

void Settings::saveToFile(const std::string& path) const {
    std::ofstream out(path);
    if (!out.is_open()) return;
    out << "# CristiVerse settings — edit and restart.\n";
    out << "screenW="      << screenW      << "\n";
    out << "screenH="      << screenH      << "\n";
    out << "fullscreen="   << (fullscreen ? 1 : 0) << "\n";
    out << "vsync="        << (vsync ? 1 : 0)      << "\n";
    out << "worldW="       << worldW       << "\n";
    out << "worldH="       << worldH       << "\n";
    out << "numAgents="    << numAgents    << "\n";
    out << "numBuildings=" << numBuildings << "\n";
    out << "numTrees="     << numTrees     << "\n";
    out << "animations="   << (animations ? 1 : 0) << "\n";
    out << "shadows="      << (shadows ? 1 : 0)    << "\n";
    out << "dayNight="     << (dayNight ? 1 : 0)   << "\n";
    out << "particles="    << (particles ? 1 : 0)  << "\n";
    out << "grass="        << (grass ? 1 : 0)      << "\n";
    out << "water="        << (water ? 1 : 0)      << "\n";
    out << "flowers="      << (flowers ? 1 : 0)    << "\n";
    out << "sound="        << (sound ? 1 : 0)      << "\n";
    out << "volume="       << volume       << "\n";
}

} // namespace cv
