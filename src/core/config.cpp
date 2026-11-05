#include "core/config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <SDL.h>

namespace gta {

namespace {

std::string trim(std::string s) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool parseBool(const std::string& value, bool& out) {
    const std::string v = lower(trim(value));
    if (v == "1" || v == "true" || v == "yes" || v == "on") {
        out = true;
        return true;
    }
    if (v == "0" || v == "false" || v == "no" || v == "off") {
        out = false;
        return true;
    }
    return false;
}

int parseInt(const std::string& value, int fallback) {
    try {
        return std::stoi(trim(value));
    } catch (...) {
        return fallback;
    }
}

float parseFloat(const std::string& value, float fallback) {
    try {
        return std::stof(trim(value));
    } catch (...) {
        return fallback;
    }
}

} // namespace

GameConfig defaultConfig() {
    GameConfig config;
    config.graphics.width = 1280;
    config.graphics.height = 720;
    config.graphics.msaa = 4;
    config.graphics.fxaa = true;
    return config;
}

bool loadConfig(const std::string& path, GameConfig& config, std::string& error) {
    if (path.empty()) return true;
    std::ifstream file(path);
    if (!file) {
        error = "cannot open config file: " + path;
        return false;
    }

    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        line = trim(line);
        if (line.empty()) continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = lower(trim(line.substr(0, eq)));
        const std::string value = trim(line.substr(eq + 1));

        if (key == "width") config.graphics.width = parseInt(value, config.graphics.width);
        else if (key == "height") config.graphics.height = parseInt(value, config.graphics.height);
        else if (key == "fullscreen") parseBool(value, config.graphics.fullscreen);
        else if (key == "vsync") parseBool(value, config.graphics.vsync);
        else if (key == "msaa") config.graphics.msaa = parseInt(value, config.graphics.msaa);
        else if (key == "fxaa") parseBool(value, config.graphics.fxaa);
        else if (key == "render_scale") config.graphics.renderScale = parseFloat(value, config.graphics.renderScale);
        else if (key == "camera_zoom") config.graphics.cameraZoom = parseFloat(value, config.graphics.cameraZoom);
        else if (key == "volume") config.audio.masterVolume = parseFloat(value, config.audio.masterVolume);
        else if (key == "music_volume") config.audio.musicVolume = parseFloat(value, config.audio.musicVolume);
        else if (key == "engine_volume") config.audio.engineVolume = parseFloat(value, config.audio.engineVolume);
        else if (key == "audio") parseBool(value, config.audio.enabled);
        else if (key == "music") parseBool(value, config.audio.music);
        else if (key == "data_dir") config.dataDir = value;
        else if (key == "city") config.city = value;
        else if (key == "car") config.carIndex = parseInt(value, config.carIndex);
        else if (key == "debug") parseBool(value, config.showDebug);
        else if (key == "clean_roads") parseBool(value, config.cleanRoads);
        else if (key == "save_valid") parseBool(value, config.save.valid);
        else if (key == "save_city") config.save.city = value;
        else if (key == "save_mission") config.save.mission = parseInt(value, config.save.mission);
        else if (key == "save_score") config.save.score = parseInt(value, config.save.score);
        else if (key == "save_health") config.save.health = parseFloat(value, config.save.health);
        else if (key == "save_armor") config.save.armor = parseFloat(value, config.save.armor);
        else if (key == "save_weapon") config.save.weapon = parseInt(value, config.save.weapon);
        else if (key == "save_ammo0") config.save.ammo[0] = parseInt(value, config.save.ammo[0]);
        else if (key == "save_ammo1") config.save.ammo[1] = parseInt(value, config.save.ammo[1]);
        else if (key == "save_ammo2") config.save.ammo[2] = parseInt(value, config.save.ammo[2]);
        else if (key == "save_ammo3") config.save.ammo[3] = parseInt(value, config.save.ammo[3]);
        else if (key == "save_x") config.save.x = parseFloat(value, config.save.x);
        else if (key == "save_y") config.save.y = parseFloat(value, config.save.y);
        else if (key == "save_z") config.save.z = parseFloat(value, config.save.z);
        else if (key == "save_yaw") config.save.yaw = parseFloat(value, config.save.yaw);
        else if (key == "save_in_car") parseBool(value, config.save.inCar);
    }

    config.graphics.width = std::max(320, config.graphics.width);
    config.graphics.height = std::max(200, config.graphics.height);
    config.graphics.msaa = std::clamp(config.graphics.msaa, 0, 16);
    config.graphics.renderScale = std::clamp(config.graphics.renderScale, 0.25f, 2.0f);
    config.graphics.cameraZoom = std::clamp(config.graphics.cameraZoom, 0.4f, 3.0f);
    config.audio.masterVolume = std::clamp(config.audio.masterVolume, 0.0f, 1.0f);
    config.audio.musicVolume = std::clamp(config.audio.musicVolume, 0.0f, 1.0f);
    config.audio.engineVolume = std::clamp(config.audio.engineVolume, 0.0f, 1.0f);
    (void)lineNumber;
    return true;
}

bool saveConfig(const std::string& path, const GameConfig& config,
                std::string& error) {
    if (path.empty()) return true;
    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        error = "cannot write config file: " + path;
        return false;
    }
    file << "# Grand Theft Auto reimplementation configuration\n";
    file << "width = " << config.graphics.width << "\n";
    file << "height = " << config.graphics.height << "\n";
    file << "fullscreen = " << (config.graphics.fullscreen ? "true" : "false") << "\n";
    file << "vsync = " << (config.graphics.vsync ? "true" : "false") << "\n";
    file << "msaa = " << config.graphics.msaa << "\n";
    file << "fxaa = " << (config.graphics.fxaa ? "true" : "false") << "\n";
    file << "render_scale = " << config.graphics.renderScale << "\n";
    file << "camera_zoom = " << config.graphics.cameraZoom << "\n";
    file << "volume = " << config.audio.masterVolume << "\n";
    file << "music_volume = " << config.audio.musicVolume << "\n";
    file << "engine_volume = " << config.audio.engineVolume << "\n";
    file << "audio = " << (config.audio.enabled ? "true" : "false") << "\n";
    file << "music = " << (config.audio.music ? "true" : "false") << "\n";
    file << "city = " << config.city << "\n";
    file << "car = " << config.carIndex << "\n";
    file << "debug = " << (config.showDebug ? "true" : "false") << "\n";
    file << "clean_roads = " << (config.cleanRoads ? "true" : "false") << "\n";
    file << "save_valid = " << (config.save.valid ? "true" : "false") << "\n";
    if (config.save.valid) {
        file << "save_city = " << config.save.city << "\n";
        file << "save_mission = " << config.save.mission << "\n";
        file << "save_score = " << config.save.score << "\n";
        file << "save_health = " << config.save.health << "\n";
        file << "save_armor = " << config.save.armor << "\n";
        file << "save_weapon = " << config.save.weapon << "\n";
        for (int i = 0; i < 4; ++i) {
            file << "save_ammo" << i << " = " << config.save.ammo[i] << "\n";
        }
        file << "save_x = " << config.save.x << "\n";
        file << "save_y = " << config.save.y << "\n";
        file << "save_z = " << config.save.z << "\n";
        file << "save_yaw = " << config.save.yaw << "\n";
        file << "save_in_car = " << (config.save.inCar ? "true" : "false") << "\n";
    }
    if (!config.dataDir.empty()) file << "data_dir = " << config.dataDir << "\n";
    return static_cast<bool>(file);
}

std::string defaultConfigPath() {
    char* pref = SDL_GetPrefPath("dos_gta_recomp", "dos_gta_recomp");
    if (!pref) return {};
    std::string path(pref);
    SDL_free(pref);
    path += "config.ini";
    return path;
}

} // namespace gta
