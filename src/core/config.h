#pragma once

#include <string>

namespace gta {

struct GraphicsConfig {
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
    bool vsync = true;
    int msaa = 4;          // 0, 2, 4, 8, 16
    bool fxaa = true;
    float renderScale = 1.0f;
    float cameraZoom = 1.0f;
};

struct AudioConfig {
    float masterVolume = 0.7f;
    float musicVolume = 0.60f;
    float engineVolume = 0.25f;
    bool enabled = true;
    bool music = true;
};

struct GameConfig {
    GraphicsConfig graphics;
    AudioConfig audio;
    std::string dataDir;   // explicit --data path, empty = auto-detect
    std::string city = "NYC";
    int carIndex = 0;
    bool showDebug = false;
};

GameConfig defaultConfig();

// Simple "key = value" text configuration.  Unknown keys are ignored so old
// configuration files keep working after upgrades.
bool loadConfig(const std::string& path, GameConfig& config, std::string& error);
bool saveConfig(const std::string& path, const GameConfig& config,
                std::string& error);

// Per-user configuration location (SDL pref path), or an empty string.
std::string defaultConfigPath();

} // namespace gta
