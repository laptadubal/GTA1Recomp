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
    float cameraZoom = 0.85f;
};

struct AudioConfig {
    float masterVolume = 0.7f;
    float musicVolume = 0.60f;
    float engineVolume = 0.25f;
    bool enabled = true;
    bool music = true;
};

// Saved single-player campaign, written to config.ini.  A modern convenience:
// the DOS original had no save games.
struct SaveGame {
    bool valid = false;
    std::string city = "NYC";
    int mission = 0;
    int score = 0;
    float health = 100.0f;
    float armor = 0.0f;
    int weapon = 0;
    int ammo[4] = {40, 0, 0, 0};
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float yaw = 0.0f;
    bool inCar = false;
};

struct GameConfig {
    GraphicsConfig graphics;
    AudioConfig audio;
    std::string dataDir;   // explicit --data path, empty = auto-detect
    std::string city = "NYC";
    int carIndex = 0;
    bool showDebug = false;
    SaveGame save;
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
