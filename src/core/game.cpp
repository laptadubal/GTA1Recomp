#include "core/game.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace gta {

namespace {

CityVertex makeVertex(const Vec3& p, float u, float v, float r, float g, float b,
                      float a) {
    CityVertex vertex{};
    vertex.x = p.x;
    vertex.y = p.y;
    vertex.z = p.z;
    vertex.u = u;
    vertex.v = v;
    vertex.r = r;
    vertex.g = g;
    vertex.b = b;
    vertex.a = a;
    return vertex;
}

std::string formatTime(float seconds) {
    const int total = std::max(0, static_cast<int>(seconds));
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", total / 60, total % 60);
    return buffer;
}

std::string percentText(float value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d%%",
                  static_cast<int>(std::round(value * 100.0f)));
    return buffer;
}

std::string cityDisplayName(const std::string& city) {
    if (city == "NYC") return "LIBERTY CITY";
    if (city == "SANB") return "SAN ANDREAS";
    if (city == "MIAMI") return "VICE CITY";
    return city;
}

} // namespace

Game::~Game() { shutdown(); }

bool Game::init(Renderer& renderer, const GameConfig& config, std::string& error) {
    renderer_ = &renderer;
    config_ = config;
    showDebug_ = config.showDebug;
    selectedCity_ = config.city.empty() ? "NYC" : config.city;
    std::transform(selectedCity_.begin(), selectedCity_.end(),
                   selectedCity_.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    if (!font_.create(error)) return false;

    glGenVertexArrays(1, &carVao_);
    glGenBuffers(1, &carVbo_);
    glBindVertexArray(carVao_);
    glBindBuffer(GL_ARRAY_BUFFER, carVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CityVertex),
                          reinterpret_cast<void*>(offsetof(CityVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(CityVertex),
                          reinterpret_cast<void*>(offsetof(CityVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(CityVertex),
                          reinterpret_cast<void*>(offsetof(CityVertex, r)));

    glGenVertexArrays(1, &quadVao_);
    glGenBuffers(1, &quadVbo_);
    glBindVertexArray(quadVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CityVertex),
                          reinterpret_cast<void*>(offsetof(CityVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(CityVertex),
                          reinterpret_cast<void*>(offsetof(CityVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(CityVertex),
                          reinterpret_cast<void*>(offsetof(CityVertex, r)));
    glBindVertexArray(0);

    if (!loadCity(selectedCity_, error)) return false;
    loadOriginalMenuAssets();
    loadMissionData();
    minimapFrame_ = createMinimapFrameTexture(256);
    minimapFrameSize_ = 256;

    screen_ = Screen::Intro;
    introTime_ = 0.0f;
    return true;
}

void Game::shutdown() {
    if (carVbo_) {
        glDeleteBuffers(1, &carVbo_);
        carVbo_ = 0;
    }
    if (carVao_) {
        glDeleteVertexArrays(1, &carVao_);
        carVao_ = 0;
    }
    if (quadVbo_) {
        glDeleteBuffers(1, &quadVbo_);
        quadVbo_ = 0;
    }
    if (quadVao_) {
        glDeleteVertexArrays(1, &quadVao_);
        quadVao_ = 0;
    }
    font_.destroy();
    menuFont_.destroy();
    bigFont_.destroy();
    missionFont_.destroy();
    missionHeaderFont_.destroy();
    keyFont_.destroy();
    cutTextFont_.destroy();
    if (minimapFrame_) {
        glDeleteTextures(1, &minimapFrame_);
        minimapFrame_ = 0;
    }
    for (GLuint& texture : logoTextures_) {
        if (texture) {
            glDeleteTextures(1, &texture);
            texture = 0;
        }
    }
    for (GLuint& texture : cutsceneTextures_) {
        if (texture) {
            glDeleteTextures(1, &texture);
            texture = 0;
        }
    }
    if (dmaLogo_) {
        glDeleteTextures(1, &dmaLogo_);
        dmaLogo_ = 0;
    }
    if (bmgLogo_) {
        glDeleteTextures(1, &bmgLogo_);
        bmgLogo_ = 0;
    }
    if (menuUpper_) {
        glDeleteTextures(1, &menuUpper_);
        menuUpper_ = 0;
    }
    if (menuLower_) {
        glDeleteTextures(1, &menuLower_);
        menuLower_ = 0;
    }
    world_.destroy();
    assets_.destroy();
}

bool Game::setupCar(std::string& error) {
    const auto& cars = assets_.gry().cars;
    if (cars.empty()) {
        error = "the graphics archive contains no car definitions";
        return false;
    }
    carIndex_ = std::clamp(config_.carIndex, 0, static_cast<int>(cars.size()) - 1);
    const GryCarInfo& car = cars[carIndex_];
    int sprite = assets_.gry().carSpriteIndex(car.sprNum);
    if (sprite < 0 || sprite >= static_cast<int>(assets_.gry().sprites.size())) {
        sprite = assets_.gry().carSpriteIndex(cars[0].sprNum);
    }
    if (sprite < 0 || sprite >= static_cast<int>(assets_.gry().sprites.size())) {
        error = "unable to locate a car sprite in the graphics archive";
        return false;
    }
    spriteIndex_ = sprite;
    const GrySpriteInfo& info = assets_.gry().sprites[sprite];
    vehicle_.setSprite(sprite, info.w, info.h, car.model);
    carModel_ = car.model;
    char label[64];
    std::snprintf(label, sizeof(label), "MODEL %d", carModel_);
    carLabel_ = label;
    assets_.carSpriteTexture(spriteIndex_);
    return true;
}

bool Game::loadCity(const std::string& city, std::string& error) {
    world_.destroy();
    assets_.destroy();

    std::string err;
    if (!assets_.load(config_.dataDir, city, err)) {
        error = err;
        return false;
    }
    if (!world_.build(assets_, err)) {
        error = err;
        return false;
    }
    if (!setupCar(err)) {
        error = err;
        return false;
    }

    lastCity_ = assets_.cityName();
    selectedCity_ = lastCity_;
    config_.city = lastCity_;
    resetCar();
    resetMission();
    menuCenter_ = Vec3{playerPosition().x, 1.0f, playerPosition().z};
    loadMusicPlaylist();
    return true;
}

void Game::loadMusicPlaylist() {
    musicTracks_.clear();
    if (assets_.dataDir().empty()) return;
    const std::filesystem::path audioDir =
        std::filesystem::path(assets_.dataDir()) / "AUDIO";
    for (int i = 0; i < 6; ++i) {
        const std::filesystem::path path =
            audioDir / (std::to_string(i) + ".WAV");
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            musicTracks_.push_back(path.string());
        }
    }
    if (musicTrack_ >= static_cast<int>(musicTracks_.size())) musicTrack_ = 0;
}

GLuint Game::loadRawTexture(const std::string& path, int width, int height) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return 0;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    const size_t needed = static_cast<size_t>(width) * height * 3;
    if (data.size() < needed) return 0;

    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (!texture) return 0;
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

GLuint Game::loadIndexedTexture(const std::string& indexFile,
                                const std::string& paletteFile, int width,
                                int height, bool paletteSixBit) {
    std::ifstream indexStream(indexFile, std::ios::binary);
    std::ifstream paletteStream(paletteFile, std::ios::binary);
    if (!indexStream || !paletteStream) return 0;
    std::vector<uint8_t> index((std::istreambuf_iterator<char>(indexStream)),
                               std::istreambuf_iterator<char>());
    std::vector<uint8_t> palette(
        (std::istreambuf_iterator<char>(paletteStream)),
        std::istreambuf_iterator<char>());
    if (index.size() < static_cast<size_t>(width) * height ||
        palette.size() < 768) {
        return 0;
    }

    auto expand = [&](uint8_t value) -> uint8_t {
        if (paletteSixBit) {
            return static_cast<uint8_t>((value << 2) | (value >> 4));
        }
        return value;
    };

    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
    for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
        const uint8_t entry = index[i];
        rgba[i * 4 + 0] = expand(palette[entry * 3 + 0]);
        rgba[i * 4 + 1] = expand(palette[entry * 3 + 1]);
        rgba[i * 4 + 2] = expand(palette[entry * 3 + 2]);
        rgba[i * 4 + 3] = 255;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (!texture) return 0;
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

void Game::loadOriginalMenuAssets() {
    const std::string dataDir = assets_.dataDir();
    if (dataDir.empty()) return;
    const std::filesystem::path base(dataDir);

    menuUpper_ = loadRawTexture((base / "F_UPPER.RAW").string(), 640, 168);
    menuLower_ = loadRawTexture((base / "F_LOWER0.RAW").string(), 640, 312);
    for (int i = 0; i < 8; ++i) {
        logoTextures_[i] = loadRawTexture(
            (base / ("F_LOGO" + std::to_string(i) + ".RAW")).string(), 640, 168);
    }
    for (int i = 0; i < 6; ++i) {
        const std::string name = "CUT" + std::to_string(i);
        cutsceneTextures_[i] = loadIndexedTexture(
            (base / (name + ".RAT")).string(),
            (base / (name + ".ACT")).string(), 640, 480, false);
    }
    dmaLogo_ = loadRawTexture((base / "F_DMA.RAW").string(), 78, 109);
    bmgLogo_ = loadRawTexture((base / "F_BMG.RAW").string(), 50, 100);
    std::string ignored;
    menuFont_.load((base / "BIG1.FON").string(), ignored);
    bigFont_.load((base / "BIG2.FON").string(), ignored);
    missionFont_.load((base / "F_MTEXT.FON").string(), ignored);
    missionHeaderFont_.load((base / "F_MHEAD.FON").string(), ignored);
    keyFont_.load((base / "F_KEY.FON").string(), ignored);
    cutTextFont_.load((base / "CUTTEXT.FON").string(), ignored);

    std::ifstream fxtFile(base / "ENGLISH.FXT", std::ios::binary);
    if (fxtFile) {
        std::vector<uint8_t> encoded(
            (std::istreambuf_iterator<char>(fxtFile)),
            std::istreambuf_iterator<char>());
        std::string decoded;
        decoded.reserve(encoded.size());
        for (uint8_t value : encoded) {
            decoded.push_back(static_cast<char>((value - 1) & 0xff));
        }
        size_t start = 0;
        while (start < decoded.size()) {
            size_t end = decoded.find('\0', start);
            if (end == std::string::npos) end = decoded.size();
            const std::string entry = decoded.substr(start, end - start);
            if (!entry.empty() && entry[0] == '[') {
                const size_t close = entry.find(']');
                if (close != std::string::npos) {
                    fxt_[entry.substr(1, close - 1)] = entry.substr(close + 1);
                }
            }
            start = end + 1;
        }
    }

    originalAssets_ = menuUpper_ != 0 && menuLower_ != 0 &&
                      logoTextures_[0] != 0;
}

void Game::playMusicTrack(int index, AudioManager& audio) {
    if (musicTracks_.empty()) return;
    const int count = static_cast<int>(musicTracks_.size());
    musicTrack_ = ((index % count) + count) % count;
    std::string error;
    if (!audio.playMusicFile(musicTracks_[musicTrack_], error)) {
        setStatus("MUSIC ERROR: " + error);
    }
}

void Game::setStatus(const std::string& text) {
    statusMessage_ = text;
    statusTimer_ = 3.5f;
}

void Game::resetCar() {
    const Vec3 spawn = world_.findSpawn(108, 150);
    const int cx = static_cast<int>(std::floor(spawn.x));
    const int cz = static_cast<int>(std::floor(spawn.z));
    const int base = world_.roadSurface(cx, cz);
    struct Direction {
        int dx, dz;
        float yaw;
    };
    const Direction directions[4] = {
        {0, -1, 0.0f},
        {1, 0, kPi * 0.5f},
        {0, 1, kPi},
        {-1, 0, -kPi * 0.5f},
    };
    int bestRun = -1;
    float bestYaw = 0.0f;
    int bestDx = 0, bestDz = -1;
    for (const Direction& dir : directions) {
        int run = 0;
        int previous = base;
        for (int step = 1; step <= 24; ++step) {
            const int surface =
                world_.roadSurface(cx + dir.dx * step, cz + dir.dz * step);
            if (surface <= 0 || std::abs(surface - previous) > 1) break;
            previous = surface;
            ++run;
        }
        if (run > bestRun) {
            bestRun = run;
            bestYaw = dir.yaw;
            bestDx = dir.dx;
            bestDz = dir.dz;
        }
    }

    inCar_ = false;
    footPos_ = spawn;
    footYaw_ = bestYaw;
    footAnimTime_ = 0.0f;
    footFrame_ = 620;

    // Park the car a couple of cells down the road so the player starts on
    // foot, as in the original game.
    Vec3 parked = spawn;
    for (int step = 2; step <= 4; ++step) {
        const int tx = cx + bestDx * step;
        const int tz = cz + bestDz * step;
        const int surface = world_.roadSurface(tx, tz);
        if (surface > 0 && std::abs(surface - base) <= 1) {
            parked = Vec3{tx + 0.5f, static_cast<float>(surface), tz + 0.5f};
            break;
        }
    }
    vehicle_.reset(parked);
    vehicle_.setYaw(bestYaw);
    carHealth_ = carHealthMax_;
    carSmokeTimer_ = 0.0f;
    lastCarSpeed_ = 0.0f;
    camera_.snapTo(footPos_);
}

void Game::spawnPickups() {
    pickups_.clear();
    const Vec3 spawn = playerPosition();
    std::uniform_int_distribution<int> coordinate(0, CityMap::kSize - 1);

    auto tooCloseToExisting = [&](const Vec3& p, float distance) {
        const float d2 = distance * distance;
        for (const Pickup& pickup : pickups_) {
            const float dx = pickup.pos.x - p.x;
            const float dz = pickup.pos.z - p.z;
            if (dx * dx + dz * dz < d2) return true;
        }
        return false;
    };

    for (int attempt = 0; attempt < 40000 &&
                            static_cast<int>(pickups_.size()) < totalPickups_;
         ++attempt) {
        const int x = coordinate(rng_);
        const int y = coordinate(rng_);
        const int surface = world_.roadSurface(x, y);
        if (surface <= 0) continue;
        const Vec3 p{x + 0.5f, static_cast<float>(surface), y + 0.5f};
        const float dx = p.x - spawn.x;
        const float dz = p.z - spawn.z;
        const float distance2 = dx * dx + dz * dz;
        if (distance2 < 9.0f * 9.0f || distance2 > 95.0f * 95.0f) continue;
        if (tooCloseToExisting(p, 5.0f)) continue;
        pickups_.push_back({p, true});
    }

    // If the city is unusual, relax the spacing rules rather than leaving an
    // unwinnable mission.
    for (int attempt = 0; attempt < 40000 &&
                            static_cast<int>(pickups_.size()) < totalPickups_;
         ++attempt) {
        const int x = coordinate(rng_);
        const int y = coordinate(rng_);
        const int surface = world_.roadSurface(x, y);
        if (surface <= 0) continue;
        const Vec3 p{x + 0.5f, static_cast<float>(surface), y + 0.5f};
        if (tooCloseToExisting(p, 2.0f)) continue;
        pickups_.push_back({p, true});
    }

    totalPickups_ = static_cast<int>(pickups_.size());
}

void Game::loadMissionData() {
    phones_.clear();
    missions_.clear();
    missionIndex_ = 0;
    activePhone_ = 0;
    targetPhone_ = -1;
    missionState_ = MissionState::Idle;
    missionTimeLeft_ = 0.0f;
    resultText_.clear();
    briefingText_.clear();

    // Resolve the named districts from the CMP navigation table through the
    // ENGLISH.FXT area strings (e.g. "001area014" -> "Ardler").
    sectorNames_.assign(assets_.map().sectors.size(), std::string());
    for (size_t i = 0; i < assets_.map().sectors.size(); ++i) {
        char key[32];
        std::snprintf(key, sizeof(key), "001area%03u",
                      static_cast<unsigned>(assets_.map().sectors[i].sample));
        const auto it = fxt_.find(key);
        if (it != fxt_.end()) sectorNames_[i] = it->second;
    }

    const std::string dataDir = assets_.dataDir();
    if (!dataDir.empty()) {
        std::ifstream file(std::filesystem::path(dataDir) / "MISSION.INI");
        const std::string city =
            assets_.cityName().empty()
                ? std::string("nyc.cmp")
                : [&]() {
                      std::string name = assets_.cityName();
                      std::transform(name.begin(), name.end(), name.begin(),
                                     [](unsigned char c) {
                                         return static_cast<char>(std::tolower(c));
                                     });
                      return name + ".cmp";
                  }();

        std::string line;
        bool pendingTitle = false;
        bool inSection = false;
        bool foundSection = false;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty() && line[0] == '[') {
                inSection = false;
                pendingTitle = true;
                continue;
            }
            if (pendingTitle) {
                if (line.empty()) continue;
                std::string title = line;
                std::transform(title.begin(), title.end(), title.begin(),
                               [](unsigned char c) {
                                   return static_cast<char>(std::tolower(c));
                               });
                if (!foundSection && title.find(city) != std::string::npos &&
                    title.find("deathmatch") == std::string::npos &&
                    title.find("cannonball") == std::string::npos) {
                    inSection = true;
                    foundSection = true;
                }
                pendingTitle = false;
                continue;
            }
            if (!inSection) continue;
            if (line.find("TELEPHONE") == std::string::npos) continue;
            int x = 0, y = 0, z = 0;
            if (std::sscanf(line.c_str(), "%*d %*d (%d , %d , %d)",
                            &x, &y, &z) != 3) {
                continue;
            }
            if (x < 0 || y < 0 || x >= CityMap::kSize || y >= CityMap::kSize)
                continue;

            int surface = world_.roadSurface(x, y);
            int bestX = x, bestY = y;
            if (surface <= 0) {
                bool found = false;
                for (int radius = 1; radius <= 8 && !found; ++radius) {
                    for (int dy = -radius; dy <= radius && !found; ++dy) {
                        for (int dx = -radius; dx <= radius; ++dx) {
                            if (std::max(std::abs(dx), std::abs(dy)) != radius)
                                continue;
                            const int tx = x + dx;
                            const int ty = y + dy;
                            const int s = world_.roadSurface(tx, ty);
                            if (s > 0) {
                                bestX = tx;
                                bestY = ty;
                                surface = s;
                                found = true;
                                break;
                            }
                        }
                    }
                }
            }
            if (surface <= 0) continue;

            // Do not spawn the player directly on top of the first payphone:
            // move it a couple of cells away so the marker is visible.
            if (phones_.empty()) {
                const Vec3 car = playerPosition();
                float dx = bestX + 0.5f - car.x;
                float dz = bestY + 0.5f - car.z;
                bool directional = false;
                if (dx * dx + dz * dz < 6.25f) {
                    const float heading = inCar_ ? vehicle_.yaw() : footYaw_;
                    const int forwardX =
                        static_cast<int>(std::lround(std::sin(heading)));
                    const int forwardZ =
                        static_cast<int>(std::lround(-std::cos(heading)));
                    for (int step = 2; step <= 7; ++step) {
                        if (forwardX == 0 && forwardZ == 0) break;
                        const int tx = bestX + forwardX * step;
                        const int ty = bestY + forwardZ * step;
                        const int s = world_.roadSurface(tx, ty);
                        if (s <= 0 || std::abs(s - surface) > 1) continue;
                        bestX = tx;
                        bestY = ty;
                        surface = s;
                        directional = true;
                        break;
                    }
                }
                if (!directional && dx * dx + dz * dz < 6.25f) {
                    for (int radius = 3; radius <= 9; ++radius) {
                        bool moved = false;
                        for (int dy = -radius; dy <= radius && !moved; ++dy) {
                            for (int dxx = -radius; dxx <= radius; ++dxx) {
                                if (std::max(std::abs(dxx), std::abs(dy)) !=
                                    radius)
                                    continue;
                                const int tx = bestX + dxx;
                                const int ty = bestY + dy;
                                const int s = world_.roadSurface(tx, ty);
                                if (s <= 0) continue;
                                const float px = tx + 0.5f - car.x;
                                const float pz = ty + 0.5f - car.z;
                                const float d2 = px * px + pz * pz;
                                if (d2 < 6.25f || d2 > 49.0f) continue;
                                bestX = tx;
                                bestY = ty;
                                surface = s;
                                moved = true;
                                break;
                            }
                        }
                        if (moved) break;
                    }
                }
            }

            const Vec3 pos{bestX + 0.5f, static_cast<float>(surface),
                           bestY + 0.5f};
            bool duplicate = false;
            for (const Phone& phone : phones_) {
                const float dx = phone.pos.x - pos.x;
                const float dz = phone.pos.z - pos.z;
                if (dx * dx + dz * dz < 2.25f) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) phones_.push_back({pos, true});
            if (phones_.size() >= 80) break;
        }
    }

    if (phones_.empty()) {
        const Vec3 spawn = playerPosition();
        phones_.push_back({Vec3{spawn.x + 2.0f, spawn.y, spawn.z}, true});
    }

    // The nearest payphone is the one that rings next.
    pickNearestPhone();
    buildMissionPlan();
}

void Game::pickNearestPhone() {
    if (phones_.empty()) return;
    const Vec3 player = playerPosition();
    int nearest = 0;
    float nearestD2 = 1e9f;
    int far = 0;
    float farD2 = 1e9f;
    for (int i = 0; i < static_cast<int>(phones_.size()); ++i) {
        const float dx = phones_[i].pos.x - player.x;
        const float dz = phones_[i].pos.z - player.z;
        const float d2 = dx * dx + dz * dz;
        if (d2 < nearestD2) {
            nearestD2 = d2;
            nearest = i;
        }
        if (d2 > 12.0f * 12.0f && d2 < farD2) {
            farD2 = d2;
            far = i;
        }
    }
    // Do not make the player answer a phone they are already standing on:
    // send them to the next one across town instead.
    activePhone_ = nearestD2 < 6.0f * 6.0f && farD2 < 1e8f ? far : nearest;
    phoneHintTimer_ = 0.0f;
    phoneRingTimer_ = 0.0f;
}

void Game::buildMissionPlan() {
    missions_.clear();

    struct Seed {
        int textId;
        MissionType type;
        float time;
        int reward;
    };
    // The briefings are the real ENGLISH.FXT strings from the original game.
    // Each city has its own block of mission text ids (1000s Liberty, 2000s
    // San Andreas, 3000s Vice City).
    static const Seed nycSeeds[] = {
        {1002, MissionType::StealDeliver, 95.0f, 500},
        {1004, MissionType::Reach, 80.0f, 300},
        {1005, MissionType::StealDeliver, 105.0f, 700},
        {1006, MissionType::Pickup, 95.0f, 800},
        {1020, MissionType::StealDeliver, 105.0f, 900},
        {1022, MissionType::Reach, 75.0f, 500},
        {1035, MissionType::StealDeliver, 115.0f, 1200},
        {1040, MissionType::KillPed, 95.0f, 1000},
        {1052, MissionType::StealDeliver, 125.0f, 1500},
        {1054, MissionType::KillPed, 85.0f, 1200},
        {1060, MissionType::Pickup, 105.0f, 1600},
        {1075, MissionType::KillPed, 95.0f, 1800},
        {1078, MissionType::StealDeliver, 105.0f, 2000},
        {1095, MissionType::StealDeliver, 105.0f, 2200},
        {1110, MissionType::StealDeliver, 125.0f, 2500},
        {1125, MissionType::StealDeliver, 105.0f, 2800},
        {1135, MissionType::KillPed, 95.0f, 3000},
        {1200, MissionType::Reach, 85.0f, 1500},
        {1245, MissionType::StealDeliver, 115.0f, 3500},
        {1300, MissionType::StealDeliver, 115.0f, 4000},
        {1355, MissionType::StealDeliver, 125.0f, 4500},
        {1380, MissionType::KillPed, 105.0f, 5000},
    };
    static const Seed sanbSeeds[] = {
        {2002, MissionType::Reach, 80.0f, 500},
        {2003, MissionType::StealDeliver, 95.0f, 600},
        {2011, MissionType::StealDeliver, 100.0f, 800},
        {2020, MissionType::Reach, 75.0f, 500},
        {2021, MissionType::StealDeliver, 105.0f, 1000},
        {2023, MissionType::Pickup, 95.0f, 900},
        {2032, MissionType::StealDeliver, 110.0f, 1100},
        {2045, MissionType::KillPed, 90.0f, 1200},
        {2050, MissionType::KillPed, 95.0f, 1400},
        {2056, MissionType::StealDeliver, 115.0f, 1700},
        {2062, MissionType::KillPed, 90.0f, 1900},
        {2078, MissionType::KillPed, 95.0f, 2100},
        {2085, MissionType::Reach, 80.0f, 1200},
        {2086, MissionType::StealDeliver, 110.0f, 2400},
        {2094, MissionType::KillPed, 95.0f, 2700},
        {2107, MissionType::StealDeliver, 110.0f, 3000},
        {2115, MissionType::StealDeliver, 120.0f, 3300},
        {2118, MissionType::StealDeliver, 110.0f, 3600},
        {2120, MissionType::Reach, 85.0f, 2000},
        {2124, MissionType::KillPed, 105.0f, 4200},
    };
    static const Seed miamiSeeds[] = {
        {3001, MissionType::KillPed, 90.0f, 600},
        {3003, MissionType::Reach, 75.0f, 400},
        {3004, MissionType::Pickup, 95.0f, 700},
        {3005, MissionType::KillPed, 90.0f, 900},
        {3011, MissionType::Reach, 75.0f, 500},
        {3016, MissionType::StealDeliver, 100.0f, 1000},
        {3019, MissionType::StealDeliver, 100.0f, 1200},
        {3025, MissionType::Reach, 75.0f, 600},
        {3026, MissionType::KillPed, 90.0f, 1400},
        {3028, MissionType::StealDeliver, 105.0f, 1700},
        {3029, MissionType::StealDeliver, 105.0f, 2000},
        {3040, MissionType::StealDeliver, 110.0f, 2300},
        {3052, MissionType::KillPed, 95.0f, 2600},
        {3055, MissionType::Pickup, 105.0f, 2900},
        {3059, MissionType::StealDeliver, 110.0f, 3200},
        {3063, MissionType::StealDeliver, 110.0f, 3500},
        {3067, MissionType::Pickup, 100.0f, 3800},
        {3080, MissionType::StealDeliver, 115.0f, 4100},
        {3098, MissionType::KillPed, 100.0f, 4500},
        {3100, MissionType::StealDeliver, 110.0f, 5000},
        {3126, MissionType::StealDeliver, 115.0f, 5500},
        {3148, MissionType::Pickup, 105.0f, 6000},
        {3159, MissionType::StealDeliver, 115.0f, 6500},
        {3176, MissionType::KillPed, 105.0f, 7000},
    };

    const std::string city = assets_.cityName();
    const Seed* seeds = nycSeeds;
    size_t seedCount = sizeof(nycSeeds) / sizeof(nycSeeds[0]);
    if (city == "SANB") {
        seeds = sanbSeeds;
        seedCount = sizeof(sanbSeeds) / sizeof(sanbSeeds[0]);
    } else if (city == "MIAMI") {
        seeds = miamiSeeds;
        seedCount = sizeof(miamiSeeds) / sizeof(miamiSeeds[0]);
    }

    for (size_t i = 0; i < seedCount; ++i) {
        const Seed& seed = seeds[i];
        const auto it = fxt_.find(std::to_string(seed.textId));
        if (it == fxt_.end() || it->second.empty()) continue;
        MissionPlan plan;
        plan.briefing = it->second;
        plan.district = districtForText(plan.briefing, &plan.sub);
        plan.type = inferMissionType(plan.briefing, seed.type);
        plan.timeLimit = seed.time;
        plan.reward = seed.reward;
        missions_.push_back(std::move(plan));
    }

    if (missions_.empty()) {
        // A data-less installation still gets a playable loop.
        static const char* fallback[] = {
            "A phone's ringing. Answer it and there'll be work for you.",
            "One of our cars has gone missing. Find it and bring it back.",
            "There's a snitch in the city. Make sure he doesn't talk.",
            "A package needs moving. Keep it clean and keep your foot down.",
        };
        for (int i = 0; i < 8; ++i) {
            MissionPlan plan;
            plan.briefing = fallback[i % 4];
            plan.type = static_cast<MissionType>(i % 5);
            plan.timeLimit = 100.0f;
            plan.reward = 500 + i * 250;
            missions_.push_back(std::move(plan));
        }
    }
    if (missionIndex_ < 0) missionIndex_ = 0;
    if (!missions_.empty() &&
        missionIndex_ >= static_cast<int>(missions_.size())) {
        missionIndex_ = 0;
    }
}

bool Game::chooseRoadLocationInDistrict(int district, int sub, Vec3& out) {
    const auto& sectors = assets_.map().sectors;
    if (district < 0 || district >= static_cast<int>(sectors.size())) return false;
    const MapSector& sector = sectors[static_cast<size_t>(district)];
    const int fullX0 = sector.x;
    const int fullY0 = sector.y;
    const int fullX1 = std::min(CityMap::kSize - 1, sector.x + sector.w);
    const int fullY1 = std::min(CityMap::kSize - 1, sector.y + sector.h);

    auto pickInRect = [&](int x0, int y0, int x1, int y1, Vec3& result) {
        if (x1 < x0 || y1 < y0) return false;
        std::uniform_int_distribution<int> rx(x0, x1);
        std::uniform_int_distribution<int> ry(y0, y1);
        for (int attempt = 0; attempt < 3000; ++attempt) {
            const int x = rx(rng_);
            const int y = ry(rng_);
            const int surface = world_.roadSurface(x, y);
            if (surface <= 0) continue;
            result = Vec3{x + 0.5f, static_cast<float>(surface), y + 0.5f};
            return true;
        }
        return false;
    };

    // Sub-area of the district mentioned in the briefing (north-east park,
    // west docks, ...).  Halves are precise enough for mission placement.
    int x0 = fullX0, y0 = fullY0, x1 = fullX1, y1 = fullY1;
    const int halfW = std::max(1, (fullX1 - fullX0) / 2);
    const int halfH = std::max(1, (fullY1 - fullY0) / 2);
    if (sub & 8) x1 = fullX0 + halfW;
    if (sub & 4) x0 = fullX0 + halfW;
    if (sub & 1) y1 = fullY0 + halfH;
    if (sub & 2) y0 = fullY0 + halfH;

    if (pickInRect(x0, y0, x1, y1, out)) return true;
    if (pickInRect(fullX0, fullY0, fullX1, fullY1, out)) return true;
    return false;
}

int Game::districtForText(const std::string& text, int* sub) const {
    if (sub) *sub = 0;
    const auto& sectors = assets_.map().sectors;
    if (sectorNames_.size() != sectors.size()) return -1;

    std::string lower;
    lower.reserve(text.size());
    for (char c : text) {
        lower.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }

    int best = -1;
    size_t bestLength = 0;
    size_t bestPos = 0;
    for (size_t i = 0; i < sectorNames_.size(); ++i) {
        std::string name = sectorNames_[i];
        if (name.size() < 4) continue;
        for (char& c : name) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        size_t pos = lower.find(name);
        while (pos != std::string::npos) {
            const bool leftOk =
                pos == 0 || !std::isalnum(static_cast<unsigned char>(lower[pos - 1]));
            const size_t end = pos + name.size();
            const bool rightOk =
                end >= lower.size() ||
                !std::isalnum(static_cast<unsigned char>(lower[end]));
            if (leftOk && rightOk) break;
            pos = lower.find(name, pos + 1);
        }
        if (pos == std::string::npos) continue;
        if (name.size() > bestLength) {
            best = static_cast<int>(i);
            bestLength = name.size();
            bestPos = pos;
        }
    }
    if (best < 0) return -1;
    if (sub) {
        const size_t start = bestPos > 34 ? bestPos - 34 : 0;
        const std::string before = lower.substr(start, bestPos - start);
        int bits = 0;
        if (before.find("north") != std::string::npos) bits |= 1;
        if (before.find("south") != std::string::npos) bits |= 2;
        if (before.find("east") != std::string::npos) bits |= 4;
        if (before.find("west") != std::string::npos) bits |= 8;
        *sub = bits;
    }
    return best;
}

std::string Game::districtLabel(int district, int sub) const {
    if (district < 0 || district >= static_cast<int>(sectorNames_.size()))
        return {};
    std::string label;
    if (sub & 1) label += "NORTH ";
    if (sub & 2) label += "SOUTH ";
    if (sub & 4) label += "EAST ";
    if (sub & 8) label += "WEST ";
    for (char c : sectorNames_[static_cast<size_t>(district)]) {
        label.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return label;
}

Game::MissionType Game::inferMissionType(const std::string& text,
                                         MissionType fallback) const {
    std::string lower;
    lower.reserve(text.size());
    for (char c : text) {
        lower.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    auto has = [&](const char* word) {
        return lower.find(word) != std::string::npos;
    };
    if (has("assassin") || has("murder") || has("waste") || has("kill") ||
        has("dead"))
        return MissionType::KillPed;
    if (has("blow up") || has("blow the") || has("destroy") || has("bomb"))
        return MissionType::DestroyCar;
    if (has("pick up") || has("pickup") || has("pick him") ||
        has("get him") || has("collect him"))
        return MissionType::Pickup;
    if (has("steal") || has("get the car") || has("bring the car") ||
        has("deliver the car") || has("pick up the car"))
        return MissionType::StealDeliver;
    return fallback;
}

bool Game::chooseRoadLocation(const Vec3& origin, float minDistance,
                              float maxDistance, Vec3& out) {
    const float min2 = minDistance * minDistance;
    const float max2 = maxDistance * maxDistance;
    std::uniform_int_distribution<int> coord(0, CityMap::kSize - 1);
    std::uniform_int_distribution<int> phonePick(0, 0);

    auto acceptable = [&](const Vec3& candidate) {
        const float dx = candidate.x - origin.x;
        const float dz = candidate.z - origin.z;
        const float d2 = dx * dx + dz * dz;
        if (d2 < min2 || d2 > max2) return false;
        // Keep mission locations apart from each other.
        if (missionSource_.x > 0.0f) {
            const float sx = candidate.x - missionSource_.x;
            const float sz = candidate.z - missionSource_.z;
            if (sx * sx + sz * sz < 12.0f * 12.0f) return false;
        }
        return true;
    };

    const int phoneCount = static_cast<int>(phones_.size());
    for (int attempt = 0; attempt < 8000; ++attempt) {
        Vec3 candidate;
        if (phoneCount > 0 && (attempt % 3) == 0) {
            std::uniform_int_distribution<int> pick(0, phoneCount - 1);
            candidate = phones_[static_cast<size_t>(pick(rng_))].pos;
        } else {
            const int x = coord(rng_);
            const int z = coord(rng_);
            const int surface = world_.roadSurface(x, z);
            if (surface <= 0) continue;
            candidate = Vec3{x + 0.5f, static_cast<float>(surface), z + 0.5f};
        }
        if (!acceptable(candidate)) continue;
        out = candidate;
        return true;
    }

    // Relax the constraints rather than failing outright.
    for (int attempt = 0; attempt < 8000; ++attempt) {
        const int x = coord(rng_);
        const int z = coord(rng_);
        const int surface = world_.roadSurface(x, z);
        if (surface <= 0) continue;
        const Vec3 candidate{x + 0.5f, static_cast<float>(surface), z + 0.5f};
        const float dx = candidate.x - origin.x;
        const float dz = candidate.z - origin.z;
        if (dx * dx + dz * dz < min2 * 0.25f) continue;
        out = candidate;
        return true;
    }
    out = origin;
    return false;
}

bool Game::chooseMissionLocation(float minDistance, float maxDistance,
                                 Vec3& out) {
    return chooseRoadLocation(playerPosition(), minDistance, maxDistance, out);
}

void Game::clearMissionEntities() {
    missionCar_.active = false;
    missionCar_.shake = 0.0f;
    missionCar_.hitCooldown = 0.0f;
    missionPed_.active = false;
    missionPed_.alive = true;
    missionCarTaken_ = false;
    hasPassenger_ = false;
}

void Game::setObjective(const std::string& text) {
    objectiveText_ = text;
}

void Game::startBriefing() {
    if (missionState_ != MissionState::Idle || missions_.empty()) return;
    const MissionPlan& plan =
        missions_[static_cast<size_t>(missionIndex_) % missions_.size()];
    briefingText_ = plan.briefing;
    missionType_ = plan.type;
    missionTimeLimit_ = plan.timeLimit;
    missionReward_ = plan.reward;
    missionDistrict_ = plan.district;
    missionSub_ = plan.sub;
    missionState_ = MissionState::Briefing;
    resultText_.clear();
}

void Game::beginMission() {
    if (missions_.empty()) return;
    const MissionPlan& plan =
        missions_[static_cast<size_t>(missionIndex_) % missions_.size()];
    missionState_ = MissionState::Active;
    missionType_ = plan.type;
    missionTimeLimit_ = plan.timeLimit;
    missionReward_ = plan.reward;
    missionDistrict_ = plan.district;
    missionSub_ = plan.sub;
    missionPhase_ = MissionPhase::GoToTarget;
    missionTimeLeft_ = plan.timeLimit;
    missionCarTaken_ = false;
    hasPassenger_ = false;
    missionFlash_ = 0.0f;
    clearMissionEntities();
    missionPhase_ = MissionPhase::GoToTarget;

    if (missionDistrict_ < 0 ||
        !chooseRoadLocationInDistrict(missionDistrict_, missionSub_,
                                      missionSource_)) {
        if (!chooseMissionLocation(24.0f, 82.0f, missionSource_)) {
            const Vec3 player = playerPosition();
            missionSource_ = Vec3{player.x + 20.0f, player.y, player.z};
        }
    }
    missionDest_ = missionSource_;
    targetPos_ = missionSource_;

    const std::string where =
        missionDistrict_ >= 0
            ? " IN " + districtLabel(missionDistrict_, missionSub_)
            : std::string();

    auto roadYaw = [&](const Vec3& pos) {
        const int cx = static_cast<int>(std::floor(pos.x));
        const int cz = static_cast<int>(std::floor(pos.z));
        const int base = world_.roadSurface(cx, cz);
        struct Dir {
            int dx, dz;
            float yaw;
        };
        const Dir dirs[4] = {{0, -1, 0.0f},
                             {1, 0, kPi * 0.5f},
                             {0, 1, kPi},
                             {-1, 0, -kPi * 0.5f}};
        int bestRun = -1;
        float bestYaw = 0.0f;
        for (const Dir& dir : dirs) {
            int run = 0;
            int previous = base;
            for (int step = 1; step <= 16; ++step) {
                const int surface =
                    world_.roadSurface(cx + dir.dx * step, cz + dir.dz * step);
                if (surface <= 0 || std::abs(surface - previous) > 1) break;
                previous = surface;
                ++run;
            }
            if (run > bestRun) {
                bestRun = run;
                bestYaw = dir.yaw;
            }
        }
        return bestYaw;
    };

    switch (missionType_) {
    case MissionType::Reach:
        setObjective("GO TO THE MARKER" + where);
        break;
    case MissionType::StealDeliver: {
        const int sprite = randomCarSprite();
        if (sprite < 0) {
            setObjective("STEAL THE CAR" + where);
            break;
        }
        missionCar_.active = true;
        missionCar_.spriteIndex = sprite;
        missionCar_.pos = missionSource_;
        missionCar_.yaw = roadYaw(missionSource_);
        const auto& info = assets_.gry().sprites[sprite];
        missionCar_.pixelWidth = info.w;
        missionCar_.pixelHeight = info.h;
        missionCar_.width = info.w / 64.0f * 1.35f;
        missionCar_.height = info.h / 64.0f * 1.35f;
        missionCar_.health = 120;
        assets_.spriteTexture(sprite);
        setObjective("STEAL THE CAR" + where);
        break;
    }
    case MissionType::DestroyCar: {
        const int sprite = randomCarSprite();
        if (sprite < 0) {
            setObjective("DESTROY THE CAR" + where);
            break;
        }
        missionCar_.active = true;
        missionCar_.spriteIndex = sprite;
        missionCar_.pos = missionSource_;
        missionCar_.yaw = roadYaw(missionSource_);
        const auto& info = assets_.gry().sprites[sprite];
        missionCar_.pixelWidth = info.w;
        missionCar_.pixelHeight = info.h;
        missionCar_.width = info.w / 64.0f * 1.35f;
        missionCar_.height = info.h / 64.0f * 1.35f;
        missionCar_.health = 120;
        assets_.spriteTexture(sprite);
        setObjective("DESTROY THE CAR" + where);
        break;
    }
    case MissionType::KillPed: {
        const int sprite = randomPedSprite();
        missionPed_.active = true;
        missionPed_.alive = true;
        missionPed_.health = 100.0f;
        missionPed_.spriteIndex = sprite;
        missionPed_.pos = missionSource_;
        missionPed_.yaw = roadYaw(missionSource_);
        const auto& info = assets_.gry().sprites[sprite];
        missionPed_.width = info.w / 64.0f * 1.5f;
        missionPed_.height = info.h / 64.0f * 1.5f;
        assets_.spriteTexture(sprite);
        setObjective("WASTE THE TARGET" + where);
        break;
    }
    case MissionType::Pickup: {
        const int sprite = randomPedSprite();
        missionPed_.active = true;
        missionPed_.alive = true;
        missionPed_.spriteIndex = sprite;
        missionPed_.pos = missionSource_;
        missionPed_.yaw = roadYaw(missionSource_);
        const auto& info = assets_.gry().sprites[sprite];
        missionPed_.width = info.w / 64.0f * 1.5f;
        missionPed_.height = info.h / 64.0f * 1.5f;
        assets_.spriteTexture(sprite);
        setObjective("PICK UP THE CONTACT" + where);
        break;
    }
    }
}

void Game::takeMissionCar() {
    if (!missionCar_.active) return;
    spriteIndex_ = missionCar_.spriteIndex;
    vehicle_.reset(missionCar_.pos);
    vehicle_.setYaw(missionCar_.yaw);
    vehicle_.setSprite(missionCar_.spriteIndex, missionCar_.pixelWidth,
                       missionCar_.pixelHeight, 0);
    assets_.carSpriteTexture(missionCar_.spriteIndex);
    carLabel_ = "MISSION CAR";
    footPos_ = missionCar_.pos;
    inCar_ = true;
    missionCar_.active = false;
    missionCarTaken_ = true;
    missionPhase_ = MissionPhase::Deliver;
    missionSource_ = missionCar_.pos;
    if (!chooseRoadLocation(missionSource_, 38.0f, 120.0f, missionDest_)) {
        missionDest_ = missionSource_;
    }
    targetPos_ = missionDest_;
    missionTimeLeft_ = std::max(missionTimeLeft_, 45.0f);
    setObjective("DELIVER THE CAR TO THE MARKER");
    setStatus("CAR STOLEN - GET MOVING");
    camera_.snapTo(vehicle_.position());
}

void Game::pickupMissionPassenger() {
    if (!missionPed_.active || hasPassenger_) return;
    missionPed_.active = false;
    hasPassenger_ = true;
    missionPhase_ = MissionPhase::Deliver;
    missionSource_ = playerPosition();
    if (!chooseRoadLocation(missionSource_, 35.0f, 115.0f, missionDest_)) {
        missionDest_ = missionSource_;
    }
    targetPos_ = missionDest_;
    missionTimeLeft_ = std::max(missionTimeLeft_, 45.0f);
    setObjective("TAKE THE PASSENGER TO THE MARKER");
    setStatus("PASSENGER ABOARD");
}

void Game::completeMission(bool success, AudioManager& audio) {
    missionState_ = MissionState::Result;
    resultTimer_ = 3.4f;
    missionFlash_ = success ? 0.55f : 0.35f;
    if (success) {
        score_ += missionReward_;
        audio.playPickupSound();
        resultText_ = "MISSION COMPLETE  +$" + std::to_string(missionReward_);
        ++missionIndex_;
        if (missionIndex_ >= static_cast<int>(missions_.size())) {
            missionComplete_ = true;
        }
    } else {
        audio.playCrashSound();
        resultText_ = "MISSION FAILED";
    }
    clearMissionEntities();
    missionPhase_ = MissionPhase::None;
    objectiveText_.clear();
}

void Game::updateMissionEntities(float dt) {
    if (missionCar_.shake > 0.0f) {
        missionCar_.shake = std::max(0.0f, missionCar_.shake - dt * 2.5f);
    }
    if (missionCar_.hitCooldown > 0.0f) {
        missionCar_.hitCooldown = std::max(0.0f, missionCar_.hitCooldown - dt);
    }
    if (missionPed_.active && missionPed_.alive) {
        const Vec3 player = playerPosition();
        const float dx = player.x - missionPed_.pos.x;
        const float dz = player.z - missionPed_.pos.z;
        if (dx * dx + dz * dz < 16.0f * 16.0f) {
            missionPed_.yaw = std::atan2(dx, -dz);
        }
    }
}

void Game::updateMission(float dt, InputManager& input, AudioManager& audio) {
    if (missionState_ == MissionState::Briefing) {
        if (input.pressed(GameAction::MenuSelect)) {
            audio.playUiClick();
            beginMission();
        } else if (input.pressed(GameAction::MenuBack)) {
            audio.playUiClick();
            missionState_ = MissionState::Idle;
            briefingText_.clear();
        }
        return;
    }

    if (missionState_ == MissionState::Result) {
        resultTimer_ -= dt;
        if (resultTimer_ <= 0.0f || input.pressed(GameAction::MenuSelect) ||
            input.pressed(GameAction::MenuBack)) {
            missionState_ = MissionState::Idle;
            resultText_.clear();
            if (missionComplete_) {
                screen_ = Screen::MissionComplete;
                menuIndex_ = 0;
                audio.stopMusic();
                return;
            }
            pickNearestPhone();
            setObjective("FIND A RINGING PAYPHONE");
        }
        return;
    }

    if (missionState_ == MissionState::Active) {
        missionTimeLeft_ -= dt;
        const Vec3 player = playerPosition();

        auto distanceTo = [&](const Vec3& p) {
            const float dx = p.x - player.x;
            const float dz = p.z - player.z;
            return std::sqrt(dx * dx + dz * dz);
        };

        if (missionTimeLeft_ <= 0.0f) {
            completeMission(false, audio);
            return;
        }

        switch (missionType_) {
        case MissionType::Reach:
            if (distanceTo(targetPos_) < 2.4f) {
                completeMission(true, audio);
            }
            break;
        case MissionType::StealDeliver:
            if (missionPhase_ == MissionPhase::GoToTarget) {
                if (missionCar_.active) {
                    const float distance = distanceTo(missionCar_.pos);
                    if (distance < 3.0f) {
                        hintFade_ = 1.0f;
                        if (input.pressed(GameAction::EnterVehicle) ||
                            input.pressed(GameAction::MenuSelect) ||
                            (!inCar_ && distance < 2.0f)) {
                            takeMissionCar();
                        }
                    }
                }
            } else if (missionPhase_ == MissionPhase::Deliver) {
                if (inCar_ && missionCarTaken_ && distanceTo(targetPos_) < 2.8f) {
                    completeMission(true, audio);
                }
            }
            break;
        case MissionType::Pickup:
            if (missionPhase_ == MissionPhase::GoToTarget) {
                if (missionPed_.active && inCar_ &&
                    distanceTo(missionPed_.pos) < 2.8f &&
                    std::abs(vehicle_.speed()) < 5.0f) {
                    pickupMissionPassenger();
                }
            } else if (missionPhase_ == MissionPhase::Deliver) {
                if (hasPassenger_ && distanceTo(targetPos_) < 2.8f) {
                    completeMission(true, audio);
                }
            }
            break;
        case MissionType::KillPed:
            if (missionPed_.active && missionPed_.alive) {
                const float distance = distanceTo(missionPed_.pos);
                const bool runOver = inCar_ && distance < 1.25f &&
                                     std::abs(vehicle_.speed()) > 1.2f;
                const bool onFootHit = !inCar_ && distance < 1.3f &&
                                       input.pressed(GameAction::MenuSelect);
                if (runOver || onFootHit) {
                    missionPed_.health = 0.0f;
                }
                if (missionPed_.health <= 0.0f) {
                    missionPed_.alive = false;
                    missionPed_.active = false;
                    missionFlash_ = 0.5f;
                    spawnBlood(missionPed_.pos, 14);
                    completeMission(true, audio);
                }
            }
            break;
        case MissionType::DestroyCar:
            if (missionCar_.active) {
                const float distance = distanceTo(missionCar_.pos);
                if (inCar_ && distance < 1.7f &&
                    std::abs(vehicle_.speed()) > 2.4f &&
                    missionCar_.hitCooldown <= 0.0f) {
                    missionCar_.hitCooldown = 0.55f;
                    missionCar_.shake = 0.5f;
                    missionCar_.health -= 35.0f;
                    audio.playCrashSound();
                    spawnSparks(missionCar_.pos);
                    if (missionCar_.health <= 0.0f) {
                        missionCar_.active = false;
                        missionFlash_ = 0.6f;
                        explodeAt(missionCar_.pos, 3.2f, 40, audio, true);
                        completeMission(true, audio);
                    }
                }
            }
            break;
        }
        return;
    }

    // Idle: the active payphone can be answered.
    if (phones_.empty()) return;
    const Vec3& phone = phones_[static_cast<size_t>(activePhone_)].pos;
    const Vec3 player = playerPosition();
    const float dx = phone.x - player.x;
    const float dz = phone.z - player.z;
    const bool near = dx * dx + dz * dz < 3.0f * 3.0f;
    phoneHintTimer_ = near ? 2.0f : std::max(0.0f, phoneHintTimer_ - dt);
    if (phoneHintTimer_ > 0.0f) hintFade_ = 1.0f;
    if (near) {
        phoneRingTimer_ -= dt;
        if (phoneRingTimer_ <= 0.0f) {
            audio.playSfx(13, 0.35f);
            phoneRingTimer_ = 2.6f;
        }
    } else {
        phoneRingTimer_ = 0.0f;
    }
    if (near && input.pressed(GameAction::MenuSelect)) {
        audio.playUiClick();
        startBriefing();
    }
}

void Game::resetMission() {
    pickups_.clear();
    collected_ = 0;
    totalPickups_ = 10;
    score_ = 0;
    missionTime_ = 0.0f;
    missionComplete_ = false;
    missionIndex_ = 0;
    activePhone_ = 0;
    targetPhone_ = -1;
    missionState_ = MissionState::Idle;
    missionPhase_ = MissionPhase::None;
    missionTimeLeft_ = 0.0f;
    resultText_.clear();
    briefingText_.clear();
    objectiveText_.clear();
    missionFlash_ = 0.0f;
    hintFade_ = 0.0f;
    projectiles_.clear();
    particles_.clear();
    weaponPickups_.clear();
    clearPolice();
    playerHealth_ = 100.0f;
    playerArmor_ = 0.0f;
    hurtFlash_ = 0.0f;
    wastedTimer_ = 0.0f;
    bustedTimer_ = 0.0f;
    deathFade_ = 0.0f;
    weapon_ = WeaponType::Pistol;
    for (int& rounds : ammo_) rounds = 0;
    ammo_[static_cast<int>(WeaponType::Pistol)] = 40;
    fireCooldown_ = 0.0f;
    muzzleFlash_ = 0.0f;
    carHealth_ = carHealthMax_;
    carSmokeTimer_ = 0.0f;
    wantedLevel_ = 0;
    wantedDecay_ = 0.0f;
    wantedFlash_ = 0.0f;
    sirenTimer_ = 0.0f;
    clearMissionEntities();
    spawnTraffic();
    spawnPeds();
    spawnProps();
    spawnPickups();
    spawnWeaponPickups();
    pickNearestPhone();
    setObjective("FIND A RINGING PAYPHONE");
}

void Game::beginPlay() {
    resetCar();
    resetMission();
    if (selectedCity_ != assets_.cityName()) {
        std::string error;
        if (!loadCity(selectedCity_, error)) {
            setStatus("CANNOT LOAD " + selectedCity_ + ": " + error);
            screen_ = Screen::MainMenu;
            return;
        }
        loadMissionData();
        resetCar();
        resetMission();
    }
    camera_.setZoom(config_.graphics.cameraZoom);
    camera_.setTilt(66.0f);
    camera_.snapTo(playerPosition());
    menuIndex_ = 0;
    screen_ = Screen::Playing;
    pendingGameMusic_ = true;
    setStatus("FIND A RINGING PAYPHONE");
}

void Game::startGame() { beginPlay(); }

void Game::startSelectedGame() {
    if (selectedCity_ != assets_.cityName()) {
        std::string error;
        if (!loadCity(selectedCity_, error)) {
            setStatus("CANNOT LOAD " + selectedCity_ + ": " + error);
            screen_ = Screen::MainMenu;
            return;
        }
        loadMissionData();
    }
    startCutscene(cutsceneForCity());
}

void Game::setDemo(bool enabled) {
    demo_ = enabled;
    if (enabled) {
        if (screen_ != Screen::Playing) startGame();
        musicStarted_ = true;
    }
}

bool Game::showScreen(const std::string& name) {
    if (name == "intro") {
        screen_ = Screen::Intro;
        introTime_ = 0.0f;
        return true;
    }
    if (name == "menu") {
        screen_ = Screen::MainMenu;
        return true;
    }
    if (name == "cutscene") {
        if (selectedCity_ != assets_.cityName()) {
            std::string error;
            if (!loadCity(selectedCity_, error)) return false;
            loadMissionData();
        }
        startCutscene(cutsceneForCity());
        cutsceneThenPlay_ = false;
        return true;
    }
    if (name == "settings") {
        settingsReturn_ = Screen::MainMenu;
        settingsIndex_ = 0;
        screen_ = Screen::Settings;
        return true;
    }
    if (name == "controls") {
        settingsReturn_ = Screen::MainMenu;
        screen_ = Screen::Controls;
        return true;
    }
    if (name == "map") {
        if (screen_ != Screen::Playing) startGame();
        screen_ = Screen::CityMap;
        return true;
    }
    if (name == "pause") {
        if (screen_ != Screen::Playing) startGame();
        screen_ = Screen::Paused;
        menuIndex_ = 0;
        return true;
    }
    if (name == "mission") {
        if (screen_ != Screen::Playing) startGame();
        screen_ = Screen::MissionComplete;
        return true;
    }
    if (name == "game") {
        if (screen_ != Screen::Playing) beginPlay();
        return true;
    }
    if (name == "briefing") {
        if (screen_ != Screen::Playing) startGame();
        startBriefing();
        return true;
    }
    if (name == "active") {
        if (screen_ != Screen::Playing) startGame();
        startBriefing();
        beginMission();
        return true;
    }
    return false;
}

void Game::update(float dt, InputManager& input, AudioManager& audio) {
    dt = std::clamp(dt, 0.0f, 0.1f);
    worldTime_ += dt;
    if (dt > 0.0f) {
        const float instant = 1.0f / dt;
        fps_ += (instant - fps_) * 0.08f;
    }
    screenshotFlash_ = std::max(0.0f, screenshotFlash_ - dt * 2.5f);
    statusTimer_ = std::max(0.0f, statusTimer_ - dt);

    if (input.pressed(GameAction::Screenshot)) {
        screenshotRequested_ = true;
        screenshotFlash_ = 1.0f;
    }
    handleGlobalInput(input, audio);

    switch (screen_) {
    case Screen::Intro:
        updateMenuCamera(dt);
        updateIntro(dt, input, audio);
        audio.setPaused(true);
        break;
    case Screen::Cutscene:
        updateCutscene(dt, input, audio);
        break;
    case Screen::MainMenu:
        updateMenuCamera(dt);
        updateMainMenu(dt, input, audio);
        audio.setPaused(true);
        break;
    case Screen::Settings:
        updateMenuCamera(dt);
        updateSettings(dt, input, audio, false);
        audio.setPaused(true);
        break;
    case Screen::Controls:
        updateMenuCamera(dt);
        updateControls(input, audio);
        audio.setPaused(true);
        break;
    case Screen::Playing:
        updatePlaying(dt, input, audio);
        break;
    case Screen::CityMap:
        updateCityMap(input, audio);
        break;
    case Screen::Paused:
        updatePaused(input, audio);
        audio.setPaused(true);
        break;
    case Screen::PausedSettings:
        updateSettings(dt, input, audio, true);
        audio.setPaused(true);
        break;
    case Screen::MissionComplete:
        updateMissionComplete(input, audio);
        audio.setPaused(true);
        break;
    }
}

void Game::handleGlobalInput(InputManager& input, AudioManager& audio) {
    if (input.pressed(GameAction::ToggleFullscreen) && renderer_) {
        renderer_->toggleFullscreen();
        config_.graphics.fullscreen = renderer_->fullscreen();
    }
    if (input.pressed(GameAction::ToggleFxaa) && renderer_) {
        renderer_->setFxaa(!renderer_->fxaaEnabled());
        config_.graphics.fxaa = renderer_->fxaaEnabled();
    }
    if (input.pressed(GameAction::ToggleDebug)) showDebug_ = !showDebug_;
    if (input.pressed(GameAction::ToggleMinimap)) {
        minimapEnabled_ = !minimapEnabled_;
        setStatus(minimapEnabled_ ? "MINIMAP ON" : "MINIMAP OFF");
    }
    if (input.pressed(GameAction::MusicNext)) {
        playMusicTrack(musicTrack_ + 1, audio);
        musicStarted_ = true;
        if (!musicTracks_.empty()) {
            setStatus("MUSIC TRACK " + std::to_string(musicTrack_ + 1));
        }
    }
}

void Game::updateIntro(float dt, InputManager& input, AudioManager& audio) {
    (void)audio;
    introTime_ += dt;
    const bool skip = input.pressed(GameAction::MenuSelect) ||
                      input.pressed(GameAction::MenuBack) ||
                      input.mousePressed();
    if (introTime_ >= 4.4f || skip) {
        screen_ = Screen::MainMenu;
        menuIndex_ = 0;
        introTime_ = 0.0f;
        menuMusicStarted_ = false;
    }
}

int Game::cutsceneForCity() const {
    const std::string& city = assets_.cityName();
    if (city == "SANB") return 1;
    if (city == "MIAMI") return 4;
    return 0;
}

int cutsceneLineCount(
    const std::unordered_map<std::string, std::string>& fxt, int index) {
    int count = 0;
    while (count < 6) {
        const std::string key =
            "cut" + std::to_string(index) + std::to_string(count);
        const auto it = fxt.find(key);
        if (it == fxt.end() || it->second.empty()) break;
        ++count;
    }
    return count;
}

void Game::startCutscene(int index) {
    cutsceneIndex_ = std::clamp(index, 0, 5);
    cutsceneLine_ = 0;
    cutsceneLineTimer_ = 0.0f;
    cutsceneFinished_ = false;
    cutsceneThenPlay_ = true;
    screen_ = Screen::Cutscene;
    musicStarted_ = true;
    menuMusicStarted_ = false;
    cutsceneMusicStarted_ = true;
}

void Game::updateCutscene(float dt, InputManager& input, AudioManager& audio) {
    if (!cutsceneMusicStarted_) {
        if (!musicTracks_.empty()) playMusicTrack(5, audio);
        cutsceneMusicStarted_ = true;
    }

    const int lineCount = cutsceneLineCount(fxt_, cutsceneIndex_);
    const bool next = input.pressed(GameAction::MenuSelect) ||
                      input.mousePressed();
    const bool skip = input.pressed(GameAction::MenuBack);

    if (lineCount <= 0) {
        // No dialogue data available: bail out after showing the art.
        cutsceneLineTimer_ += dt;
        if (cutsceneLineTimer_ > 2.5f || next || skip) {
            audio.stopMusic();
            cutsceneMusicStarted_ = false;
            if (cutsceneThenPlay_) {
                beginPlay();
            } else {
                screen_ = Screen::MainMenu;
            }
        }
        return;
    }

    cutsceneLineTimer_ += dt;
    if (next || cutsceneLineTimer_ >= 3.6f) {
        cutsceneLineTimer_ = 0.0f;
        ++cutsceneLine_;
        if (cutsceneLine_ >= lineCount) {
            cutsceneLine_ = lineCount - 1;
            cutsceneFinished_ = true;
        }
    }
    if (skip) cutsceneFinished_ = true;

    if (cutsceneFinished_) {
        audio.stopMusic();
        cutsceneMusicStarted_ = false;
        cutsceneFinished_ = false;
        cutsceneLine_ = 0;
        cutsceneLineTimer_ = 0.0f;
        if (cutsceneThenPlay_) {
            cutsceneThenPlay_ = false;
            beginPlay();
        } else {
            screen_ = Screen::MainMenu;
            menuIndex_ = 0;
            menuMusicStarted_ = false;
        }
    }
}

void Game::updateMenuCamera(float dt) {
    menuTime_ += dt;
    if (!renderer_) return;
    const float angle = menuTime_ * 0.025f;
    const Vec3 target{menuCenter_.x + std::sin(angle) * 5.0f, menuCenter_.y,
                      menuCenter_.z + std::cos(angle) * 5.0f};
    camera_.setViewport(renderer_->width(), renderer_->height());
    camera_.setZoom(0.9f);
    camera_.setTilt(48.0f);
    camera_.snapTo(target);
}

void Game::updateMainMenu(float dt, InputManager& input, AudioManager& audio) {
    (void)dt;
    if (!menuMusicStarted_ && !musicTracks_.empty()) {
        playMusicTrack(0, audio);
        menuMusicStarted_ = true;
    }
    const int count = 5;
    if (input.pressed(GameAction::MenuUp)) {
        menuIndex_ = (menuIndex_ + count - 1) % count;
    }
    if (input.pressed(GameAction::MenuDown)) {
        menuIndex_ = (menuIndex_ + 1) % count;
    }

    bool activate = input.pressed(GameAction::MenuSelect);
    if (input.mousePressed()) {
        const int hit = menuHitTestFrame(
            count, 214.0f, 44.0f, 150.0f, 512.0f,
            input.mouseX() * static_cast<float>(renderer_->width()) /
                std::max(1, renderer_->windowWidth()),
            input.mouseY() * static_cast<float>(renderer_->height()) /
                std::max(1, renderer_->windowHeight()));
        if (hit >= 0) {
            menuIndex_ = hit;
            activate = true;
        }
    }

    if (menuIndex_ == 1) {
        static const char* cities[] = {"NYC", "MIAMI", "SANB"};
        auto current = std::find(std::begin(cities), std::end(cities), selectedCity_);
        int index = current == std::end(cities)
                        ? 0
                        : static_cast<int>(current - std::begin(cities));
        if (input.pressed(GameAction::MenuLeft)) {
            selectedCity_ = cities[(index + 2) % 3];
        }
        if (input.pressed(GameAction::MenuRight)) {
            selectedCity_ = cities[(index + 1) % 3];
        }
    }

    if (!activate) return;
    audio.playUiClick();
    switch (menuIndex_) {
    case 0:
        startSelectedGame();
        break;
    case 1: {
        static const char* cities[] = {"NYC", "MIAMI", "SANB"};
        auto current = std::find(std::begin(cities), std::end(cities), selectedCity_);
        int index = current == std::end(cities)
                        ? 0
                        : static_cast<int>(current - std::begin(cities));
        selectedCity_ = cities[(index + 1) % 3];
        break;
    }
    case 2:
        settingsReturn_ = Screen::MainMenu;
        settingsIndex_ = 0;
        screen_ = Screen::Settings;
        break;
    case 3:
        settingsReturn_ = Screen::MainMenu;
        screen_ = Screen::Controls;
        break;
    case 4:
        quit_ = true;
        break;
    default:
        break;
    }
}

void Game::updateSettings(float dt, InputManager& input, AudioManager& audio,
                          bool fromPause) {
    (void)dt;
    const int count = 8;
    if (input.pressed(GameAction::MenuUp)) {
        settingsIndex_ = (settingsIndex_ + count - 1) % count;
    }
    if (input.pressed(GameAction::MenuDown)) {
        settingsIndex_ = (settingsIndex_ + 1) % count;
    }

    auto back = [&]() {
        screen_ = fromPause ? Screen::Paused : settingsReturn_;
        menuIndex_ = 0;
    };

    auto apply = [&](int item, int direction) {
        static const int resolutions[][2] = {
            {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}};
        static const int msaaValues[] = {0, 2, 4, 8, 16};
        switch (item) {
        case 0: {
            int index = 0;
            for (int i = 0; i < 4; ++i) {
                if (resolutions[i][0] == config_.graphics.width &&
                    resolutions[i][1] == config_.graphics.height) {
                    index = i;
                    break;
                }
            }
            index = (index + direction + 4) % 4;
            config_.graphics.width = resolutions[index][0];
            config_.graphics.height = resolutions[index][1];
            if (renderer_) renderer_->setWindowSize(config_.graphics.width,
                                                    config_.graphics.height);
            break;
        }
        case 1:
            config_.graphics.fullscreen = !config_.graphics.fullscreen;
            if (renderer_) renderer_->setFullscreen(config_.graphics.fullscreen);
            break;
        case 2: {
            int index = 0;
            for (int i = 0; i < 5; ++i) {
                if (msaaValues[i] == config_.graphics.msaa) index = i;
            }
            index = (index + direction + 5) % 5;
            config_.graphics.msaa = msaaValues[index];
            if (renderer_) renderer_->setMSAA(config_.graphics.msaa);
            break;
        }
        case 3:
            config_.graphics.fxaa = !config_.graphics.fxaa;
            if (renderer_) renderer_->setFxaa(config_.graphics.fxaa);
            break;
        case 4:
            config_.audio.music = !config_.audio.music;
            audio.setMusicEnabled(config_.audio.music);
            break;
        case 5:
            config_.audio.musicVolume = std::clamp(
                config_.audio.musicVolume + direction * 0.1f, 0.0f, 1.0f);
            audio.setMusicVolume(config_.audio.musicVolume);
            break;
        case 6:
            config_.audio.engineVolume = std::clamp(
                config_.audio.engineVolume + direction * 0.1f, 0.0f, 1.0f);
            audio.setEngineVolume(config_.audio.engineVolume);
            break;
        case 7:
            back();
            break;
        default:
            break;
        }
    };

    const bool left = input.pressed(GameAction::MenuLeft);
    const bool right = input.pressed(GameAction::MenuRight);
    if (settingsIndex_ != 7) {
        if (left) {
            apply(settingsIndex_, -1);
            audio.playUiClick();
        }
        if (right) {
            apply(settingsIndex_, 1);
            audio.playUiClick();
        }
    }

    bool activate = input.pressed(GameAction::MenuSelect);
    if (input.mousePressed()) {
        const int hit = menuHitTestFrame(
            count, 150.0f, 38.0f, 150.0f, 512.0f,
            input.mouseX() * static_cast<float>(renderer_->width()) /
                std::max(1, renderer_->windowWidth()),
            input.mouseY() * static_cast<float>(renderer_->height()) /
                std::max(1, renderer_->windowHeight()));
        if (hit >= 0) {
            settingsIndex_ = hit;
            activate = true;
        }
    }
    if (activate) {
        audio.playUiClick();
        if (settingsIndex_ == 7) {
            back();
        } else {
            apply(settingsIndex_, 1);
        }
    }
    if (input.pressed(GameAction::MenuBack)) {
        audio.playUiClick();
        back();
    }
}

void Game::updateControls(InputManager& input, AudioManager& audio) {
    if (input.pressed(GameAction::MenuBack) ||
        input.pressed(GameAction::MenuSelect) || input.mousePressed()) {
        audio.playUiClick();
        screen_ = settingsReturn_;
    }
}


bool Game::nearVehicle() const {
    const float dx = vehicle_.position().x - footPos_.x;
    const float dz = vehicle_.position().z - footPos_.z;
    return !inCar_ && dx * dx + dz * dz < 2.6f * 2.6f;
}

void Game::updateOnFoot(float dt, InputManager& input, AudioManager& audio) {
    float moveX = input.steer();
    float moveZ = -input.throttle();
    const float length = std::sqrt(moveX * moveX + moveZ * moveZ);
    if (length > 1.0f) {
        moveX /= length;
        moveZ /= length;
    }

    const float speed = 2.4f;
    Vec3 next = footPos_;
    next.x += moveX * speed * dt;
    next.z += moveZ * speed * dt;

    // On foot the player can step off the road: pavements, parks and bridge
    // decks are all walkable, buildings and water are not.
    auto walkable = [&](float x, float z) {
        return world_.isPassable(x, z);
    };
    if (!walkable(next.x, next.z)) {
        Vec3 slideX{next.x, footPos_.y, footPos_.z};
        Vec3 slideZ{footPos_.x, footPos_.y, next.z};
        if (walkable(slideX.x, slideX.z)) {
            next = slideX;
        } else if (walkable(slideZ.x, slideZ.z)) {
            next = slideZ;
        } else {
            next = footPos_;
        }
    }
    const bool moving = (next.x != footPos_.x || next.z != footPos_.z);
    footPos_ = next;
    footPos_.y = static_cast<float>(
        world_.surfaceHeight(static_cast<int>(std::floor(footPos_.x)),
                             static_cast<int>(std::floor(footPos_.z))));

    if (moving && std::abs(moveX) + std::abs(moveZ) > 0.01f) {
        footYaw_ = std::atan2(moveX, -moveZ);
        footAnimTime_ += dt;
        int direction =
            static_cast<int>(std::lround(footYaw_ / (2.0f * kPi) * 8.0f));
        direction = ((direction % 8) + 8) % 8;
        footFrame_ = 620 + direction;
    }

    bool phoneNear = false;
    if (missionState_ == MissionState::Idle && !phones_.empty()) {
        const Vec3& phone = phones_[static_cast<size_t>(activePhone_)].pos;
        const float pdx = phone.x - footPos_.x;
        const float pdz = phone.z - footPos_.z;
        phoneNear = pdx * pdx + pdz * pdz < 3.0f * 3.0f;
    }
    if (input.pressed(GameAction::EnterVehicle) &&
        nearestTrafficCar(2.2f) >= 0) {
        carjackNearest(audio);
        return;
    }
    if ((input.pressed(GameAction::EnterVehicle) ||
         (input.pressed(GameAction::MenuSelect) && !phoneNear)) &&
        nearVehicle()) {
        audio.playUiClick();
        inCar_ = true;
        camera_.snapTo(vehicle_.position());
    }

    camera_.follow(Vec3{footPos_.x, footPos_.y, footPos_.z}, dt);
    if (renderer_) camera_.setViewport(renderer_->width(), renderer_->height());
    audio.setEngine(0.0f, 0.0f, false);
}

void Game::drawPlayer(Renderer& renderer) {
    if (inCar_) return;
    int base = 620;
    if (ammo_[static_cast<int>(weapon_)] > 0) {
        switch (weapon_) {
        case WeaponType::Flamethrower:
            base = 738;
            break;
        case WeaponType::Rocket:
            base = 774;
            break;
        case WeaponType::Pistol:
        case WeaponType::Uzi:
        default:
            base = 756;
            break;
        }
    }
    const int direction = ((footFrame_ - 620) % 8 + 8) % 8;
    const int frame = base + direction;
    const auto& sprites = assets_.gry().sprites;
    if (frame < 0 || frame >= static_cast<int>(sprites.size())) return;
    const int w = sprites[frame].w;
    const int h = sprites[frame].h;
    if (w <= 0 || h <= 0) return;
    drawWorldSprite(renderer, assets_.spriteTexture(frame), footPos_, footYaw_,
                    w / 64.0f * 1.7f, h / 64.0f * 1.7f, 0.03f);
}

void Game::updatePlaying(float dt, InputManager& input, AudioManager& audio) {
    if (pendingGameMusic_) {
        pendingGameMusic_ = false;
        if (!musicTracks_.empty()) playMusicTrack(1, audio);
        musicStarted_ = true;
        menuMusicStarted_ = false;
    }

    if (wastedTimer_ > 0.0f) {
        wastedTimer_ -= dt;
        deathFade_ = std::min(1.0f, deathFade_ + dt * 1.6f);
        hurtFlash_ = std::max(0.0f, hurtFlash_ - dt);
        updateParticles(dt);
        if (wastedTimer_ <= 0.0f) {
            respawnPlayer(audio);
        }
        audio.setPaused(false);
        audio.setEngine(0.0f, 0.0f, false);
        return;
    }
    if (bustedTimer_ > 0.0f) {
        bustedTimer_ -= dt;
        deathFade_ = std::min(1.0f, deathFade_ + dt * 1.6f);
        updateParticles(dt);
        if (bustedTimer_ <= 0.0f) {
            respawnPlayer(audio);
        }
        audio.setPaused(false);
        audio.setEngine(0.0f, 0.0f, false);
        return;
    }
    deathFade_ = std::max(0.0f, deathFade_ - dt * 2.0f);

    if (demo_) {
        if (missionState_ == MissionState::Idle && !phones_.empty()) {
            const Vec3& phone = phones_[static_cast<size_t>(activePhone_)].pos;
            const Vec3 player = playerPosition();
            const float dx = phone.x - player.x;
            const float dz = phone.z - player.z;
            if (dx * dx + dz * dz < 2.6f * 2.6f) {
                startBriefing();
                demoBriefTimer_ = 0.0f;
            }
        } else if (missionState_ == MissionState::Briefing) {
            demoBriefTimer_ += dt;
            if (demoBriefTimer_ > 1.1f) {
                beginMission();
                demoBriefTimer_ = 0.0f;
            }
        } else if (missionState_ == MissionState::Active &&
                   missionType_ == MissionType::StealDeliver &&
                   missionPhase_ == MissionPhase::GoToTarget &&
                   missionCar_.active) {
            const float dx = missionCar_.pos.x - playerPosition().x;
            const float dz = missionCar_.pos.z - playerPosition().z;
            if (dx * dx + dz * dz < 3.0f * 3.0f) takeMissionCar();
        }
    }

    const bool modal = missionState_ == MissionState::Briefing ||
                       missionState_ == MissionState::Result;
    if (!modal && (input.pressed(GameAction::TogglePause) ||
                   input.pressed(GameAction::MenuBack))) {
        screen_ = Screen::Paused;
        menuIndex_ = 0;
        audio.setPaused(true);
        return;
    }

    if (input.pressed(GameAction::ToggleMap)) {
        audio.playUiClick();
        screen_ = Screen::CityMap;
        return;
    }

    if (input.pressed(GameAction::ToggleCamera)) {
        cameraMode_ = (cameraMode_ + 1) % 3;
        const float tilts[3] = {66.0f, 55.0f, 78.0f};
        camera_.setTilt(tilts[cameraMode_]);
    }
    if (input.pressed(GameAction::ZoomIn)) {
        camera_.setZoom(camera_.zoom() + 0.12f);
        config_.graphics.cameraZoom = camera_.zoom();
    }
    if (input.pressed(GameAction::ZoomOut)) {
        camera_.setZoom(camera_.zoom() - 0.12f);
        config_.graphics.cameraZoom = camera_.zoom();
    }
    if (input.pressed(GameAction::Reset)) {
        resetCar();
    }
    if (input.pressed(GameAction::NextWeapon)) {
        const int count = static_cast<int>(WeaponType::Count);
        const int current = static_cast<int>(weapon_);
        bool changed = false;
        for (int step = 1; step <= count; ++step) {
            const int next = (current + step) % count;
            if (ammo_[next] > 0) {
                weapon_ = static_cast<WeaponType>(next);
                changed = true;
                setStatus(weaponName());
                break;
            }
        }
        if (!changed) setStatus("NO AMMUNITION");
    }

    if (demo_ && !inCar_) {
        inCar_ = true;
        footPos_ = vehicle_.position();
        camera_.snapTo(vehicle_.position());
    }

    VehicleControls controls;
    if (modal) {
        controls.throttle = 0.0f;
        controls.steer = 0.0f;
        controls.handbrake = true;
    } else if (inCar_ && demo_) {
        demoTime_ += dt;
        Vec3 goal;
        bool hasGoal = false;
        if (missionState_ == MissionState::Active) {
            goal = targetPos_;
            hasGoal = true;
        } else if (missionState_ == MissionState::Idle && !phones_.empty()) {
            goal = phones_[static_cast<size_t>(activePhone_)].pos;
            hasGoal = true;
        }
        if (hasGoal) {
            const float dx = goal.x - vehicle_.position().x;
            const float dz = goal.z - vehicle_.position().z;
            const float distance = std::sqrt(dx * dx + dz * dz);
            const float desired = std::atan2(dx, -dz);
            float difference = desired - vehicle_.yaw();
            while (difference > kPi) difference -= 2.0f * kPi;
            while (difference < -kPi) difference += 2.0f * kPi;
            controls.steer = std::clamp(difference * 2.0f, -1.0f, 1.0f);
            controls.throttle = distance > 2.6f ? 0.72f : 0.0f;
        } else {
            controls.throttle = 0.0f;
            controls.steer = 0.0f;
        }
        controls.handbrake = false;
    } else if (inCar_) {
        controls.throttle = input.throttle();
        controls.steer = input.steer();
        controls.handbrake = input.handbrake();
    }

    if (!modal) {
        if (inCar_) {
            const bool wantExit =
                input.pressed(GameAction::EnterVehicle) &&
                std::abs(vehicle_.speed()) < 1.0f;
            const bool canSwapCar =
                wantExit && missionCar_.active &&
                missionType_ == MissionType::StealDeliver &&
                missionPhase_ == MissionPhase::GoToTarget &&
                (std::pow(vehicle_.position().x - missionCar_.pos.x, 2.0f) +
                 std::pow(vehicle_.position().z - missionCar_.pos.z, 2.0f)) <
                    9.0f;
            if (canSwapCar) {
                takeMissionCar();
            } else if (wantExit) {
                inCar_ = false;
                const float sideX = std::cos(vehicle_.yaw()) * 0.9f;
                const float sideZ = std::sin(vehicle_.yaw()) * 0.9f;
                footPos_ = Vec3{vehicle_.position().x + sideX,
                                vehicle_.position().y,
                                vehicle_.position().z + sideZ};
                if (!world_.isDrivable(footPos_.x, footPos_.z)) {
                    footPos_ = vehicle_.position();
                }
                footYaw_ = vehicle_.yaw();
                camera_.snapTo(footPos_);
            } else {
                const float impactSpeed = std::abs(lastCarSpeed_);
                vehicle_.update(dt, controls, world_);
                if (vehicle_.crashed() && !wasCrashing_) {
                    audio.playCrashSound();
                    if (impactSpeed > 4.0f && carHealth_ > 0.0f) {
                        const float damage = (impactSpeed - 3.0f) * 2.8f;
                        spawnSparks(vehicle_.position());
                        damageCar(damage, audio);
                    }
                }
                wasCrashing_ = vehicle_.crashed();
                lastCarSpeed_ = std::abs(vehicle_.speed());

                const float yaw = vehicle_.yaw();
                const float lookAhead =
                    std::clamp(vehicle_.speed() * 0.12f, -1.2f, 2.5f);
                Vec3 cameraTarget = vehicle_.position();
                cameraTarget.x += std::sin(yaw) * lookAhead;
                cameraTarget.z += -std::cos(yaw) * lookAhead;
                camera_.follow(cameraTarget, dt);
                if (renderer_)
                    camera_.setViewport(renderer_->width(),
                                        renderer_->height());
            }
        } else {
            updateOnFoot(dt, input, audio);
        }
        updateTraffic(dt);
        updatePeds(dt);
    }

    updateMissionEntities(dt);
    updateCombat(dt, input, audio);
    const Vec3 player = playerPosition();
    missionTime_ += dt;
    for (Pickup& pickup : pickups_) {
        if (!pickup.active) continue;
        const float dx = pickup.pos.x - player.x;
        const float dz = pickup.pos.z - player.z;
        if (dx * dx + dz * dz < 0.9f * 0.9f &&
            std::abs(pickup.pos.y - player.y) < 2.0f) {
            pickup.active = false;
            ++collected_;
            score_ += 100;
            audio.playPickupSound();
            setStatus("CASH +$100");
        }
    }

    updateMission(dt, input, audio);

    audio.setPaused(false);
    audio.setEngine(inCar_ && !modal ? vehicle_.speed() : 0.0f,
                    inCar_ && !modal ? controls.throttle : 0.0f, false);
}

void Game::updateCityMap(InputManager& input, AudioManager& audio) {
    if (input.pressed(GameAction::ToggleMap) ||
        input.pressed(GameAction::MenuBack) ||
        input.pressed(GameAction::TogglePause) ||
        input.pressed(GameAction::MenuSelect) || input.mousePressed()) {
        audio.playUiClick();
        screen_ = Screen::Playing;
    }
}

void Game::updatePaused(InputManager& input, AudioManager& audio) {
    const int count = 5;
    if (input.pressed(GameAction::TogglePause) ||
        input.pressed(GameAction::MenuBack)) {
        audio.playUiClick();
        screen_ = Screen::Playing;
        menuIndex_ = 0;
        return;
    }
    if (input.pressed(GameAction::MenuUp)) {
        menuIndex_ = (menuIndex_ + count - 1) % count;
    }
    if (input.pressed(GameAction::MenuDown)) {
        menuIndex_ = (menuIndex_ + 1) % count;
    }
    bool activate = input.pressed(GameAction::MenuSelect);
    if (input.mousePressed()) {
        const int hit = menuHitTestFrame(
            count, 214.0f, 44.0f, 150.0f, 512.0f,
            input.mouseX() * static_cast<float>(renderer_->width()) /
                std::max(1, renderer_->windowWidth()),
            input.mouseY() * static_cast<float>(renderer_->height()) /
                std::max(1, renderer_->windowHeight()));
        if (hit >= 0) {
            menuIndex_ = hit;
            activate = true;
        }
    }
    if (!activate) return;
    audio.playUiClick();
    switch (menuIndex_) {
    case 0:
        screen_ = Screen::Playing;
        menuIndex_ = 0;
        break;
    case 1:
        resetCar();
        resetMission();
        screen_ = Screen::Playing;
        menuIndex_ = 0;
        break;
    case 2:
        settingsReturn_ = Screen::Paused;
        settingsIndex_ = 0;
        screen_ = Screen::PausedSettings;
        break;
    case 3:
        audio.stopMusic();
        menuMusicStarted_ = false;
        screen_ = Screen::MainMenu;
        menuIndex_ = 0;
        break;
    case 4:
        quit_ = true;
        break;
    default:
        break;
    }
}

void Game::updateMissionComplete(InputManager& input, AudioManager& audio) {
    if (input.pressed(GameAction::MenuSelect) || input.mousePressed()) {
        startGame();
    } else if (input.pressed(GameAction::MenuBack)) {
        audio.stopMusic();
        menuMusicStarted_ = false;
        screen_ = Screen::MainMenu;
        menuIndex_ = 0;
    }
}

float Game::uiScale() const {
    if (!renderer_) return 1.0f;
    return std::clamp(static_cast<float>(renderer_->height()) / 720.0f, 0.75f,
                      3.0f);
}

float Game::menuFirstY(int count, float topFraction) const {
    (void)count;
    if (!renderer_) return 0.0f;
    return static_cast<float>(renderer_->height()) * topFraction;
}

float Game::menuItemWidth() const {
    if (!renderer_) return 500.0f;
    return std::min(static_cast<float>(renderer_->width()) * 0.52f,
                    470.0f * uiScale());
}

float Game::menuItemSpacing() const { return 50.0f * uiScale(); }

Game::MenuFrame Game::menuFrame() const {
    MenuFrame frame;
    if (!renderer_) return frame;
    const float w = static_cast<float>(renderer_->width());
    const float h = static_cast<float>(renderer_->height());
    frame.scale = std::min(w / 640.0f, h / 480.0f);
    frame.offsetX = (w - 640.0f * frame.scale) * 0.5f;
    frame.offsetY = (h - 480.0f * frame.scale) * 0.5f;
    return frame;
}

int Game::menuHitTestFrame(int count, float firstY43, float spacing43,
                           float xMin43, float xMax43, float x, float y) const {
    if (!renderer_) return -1;
    const MenuFrame frame = menuFrame();
    if (frame.scale <= 0.0f) return -1;
    const float localX = (x - frame.offsetX) / frame.scale;
    const float localY = (y - frame.offsetY) / frame.scale;
    if (localX < xMin43 || localX > xMax43) return -1;
    for (int i = 0; i < count; ++i) {
        const float itemY = firstY43 + i * spacing43;
        if (localY >= itemY - 8.0f && localY <= itemY + spacing43 * 0.8f) {
            return i;
        }
    }
    return -1;
}

int Game::menuHitTest(int count, float topFraction, float x, float y) const {
    if (!renderer_) return -1;
    const float width = menuItemWidth();
    const float left = (static_cast<float>(renderer_->width()) - width) * 0.5f;
    const float top = menuFirstY(count, topFraction);
    const float spacing = menuItemSpacing();
    if (x < left || x > left + width) return -1;
    for (int i = 0; i < count; ++i) {
        const float itemY = top + i * spacing;
        if (y >= itemY - 6.0f * uiScale() &&
            y <= itemY + spacing * 0.72f) {
            return i;
        }
    }
    return -1;
}

void Game::render(Renderer& renderer) {
    renderer.beginFrame();
    camera_.setViewport(renderer.width(), renderer.height());

    switch (screen_) {
    case Screen::Intro:
        renderWorldScene(renderer, false, false);
        renderIntro(renderer);
        break;
    case Screen::Cutscene:
        renderCutscene(renderer);
        break;
    case Screen::MainMenu:
        renderWorldScene(renderer, false, false);
        renderMainMenu(renderer);
        break;
    case Screen::Settings:
        renderWorldScene(renderer, false, false);
        renderSettings(renderer, false);
        break;
    case Screen::Controls:
        renderWorldScene(renderer, false, false);
        renderControls(renderer);
        break;
    case Screen::Playing:
        renderWorldScene(renderer, true, true);
        glDisable(GL_DEPTH_TEST);
        drawHud(renderer);
        drawMissionUi(renderer);
        break;
    case Screen::CityMap:
        renderCityMap(renderer);
        break;
    case Screen::Paused:
        renderWorldScene(renderer, true, true);
        glDisable(GL_DEPTH_TEST);
        drawHud(renderer);
        renderPause(renderer);
        break;
    case Screen::PausedSettings:
        renderWorldScene(renderer, true, true);
        glDisable(GL_DEPTH_TEST);
        drawHud(renderer);
        renderSettings(renderer, true);
        break;
    case Screen::MissionComplete:
        renderWorldScene(renderer, false, false);
        glDisable(GL_DEPTH_TEST);
        renderMissionComplete(renderer);
        break;
    }

    glEnable(GL_DEPTH_TEST);
    renderer.endFrame();
}

void Game::renderWorldScene(Renderer& renderer, bool withCar, bool withPickups) {
    drawWorld(renderer);
    if (withPickups) {
        drawPickups(renderer);
        drawWeaponPickups(renderer);
        drawMissionMarkers(renderer);
    }
    if (withCar) {
        drawProps(renderer);
        drawPeds(renderer);
        drawTraffic(renderer);
        drawPolice(renderer);
        drawCops(renderer);
        drawMissionTargets(renderer);
        drawPlayer(renderer);
        drawCar(renderer);
        drawProjectiles(renderer);
        drawParticles(renderer);
    }
}

void Game::drawWorld(Renderer& renderer) {
    if (!renderer.spriteShader().valid() || !assets_.atlasTexture()) return;

    Shader& shader = renderer.spriteShader();
    shader.use();
    shader.setMat4("uMVP", camera_.viewProjection());
    shader.setInt("uTexture", 0);
    shader.setInt("uAlphaTest", 0);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    renderer.whiteTexture().bind(0);
    CityVertex ground[4];
    ground[0] = makeVertex(Vec3{-1.0f, 0.0f, -1.0f}, 0, 0, 0.05f, 0.08f, 0.12f, 1.0f);
    ground[1] = makeVertex(Vec3{257.0f, 0.0f, -1.0f}, 1, 0, 0.05f, 0.08f, 0.12f, 1.0f);
    ground[2] = makeVertex(Vec3{257.0f, 0.0f, 257.0f}, 1, 1, 0.05f, 0.08f, 0.12f, 1.0f);
    ground[3] = makeVertex(Vec3{-1.0f, 0.0f, 257.0f}, 0, 1, 0.05f, 0.08f, 0.12f, 1.0f);
    const uint32_t order[6] = {0, 1, 2, 0, 2, 3};
    CityVertex tri[6];
    for (int i = 0; i < 6; ++i) tri[i] = ground[order[i]];

    glBindVertexArray(quadVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, assets_.atlasTexture());

    // The visible area follows the camera; culling whole chunks keeps the
    // close-up view cheap even though the city has 256 chunks.
    const Vec3 center = camera_.target();
    const float halfExtent = 46.0f;
    auto visible = [&](const CityChunk& chunk) {
        return chunk.maxX >= center.x - halfExtent &&
               chunk.minX <= center.x + halfExtent &&
               chunk.maxZ >= center.z - halfExtent &&
               chunk.minZ <= center.z + halfExtent;
    };

    // Pass 1: opaque city geometry.  The alpha test mode 2 ignores the
    // palette index 0 colour key so black pixels of opaque tiles stay black.
    shader.setInt("uAlphaTest", 2);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    for (const CityChunk& chunk : world_.chunks()) {
        if (!chunk.vao || chunk.opaqueIndexCount <= 0) continue;
        if (!visible(chunk)) continue;
        glBindVertexArray(chunk.vao);
        glDrawElements(GL_TRIANGLES, chunk.opaqueIndexCount, GL_UNSIGNED_INT,
                       nullptr);
    }

    // Pass 2: transparent "flat" decals (trees, road markings, graffiti).
    // Depth testing keeps them behind buildings; depth writes are disabled so
    // stacked decals blend in order.
    shader.setInt("uAlphaTest", 1);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    for (const CityChunk& chunk : world_.chunks()) {
        if (!chunk.vao) continue;
        const GLsizei count = chunk.indexCount - chunk.opaqueIndexCount;
        if (count <= 0) continue;
        if (!visible(chunk)) continue;
        glBindVertexArray(chunk.vao);
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(
                           static_cast<uintptr_t>(chunk.opaqueIndexCount) *
                           sizeof(uint32_t)));
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

void Game::drawWorldSprite(Renderer& renderer, GLuint texture, const Vec3& pos,
                           float yaw, float width, float height, float yOffset,
                           float r, float g, float b, float a) {
    if (!texture || width <= 0.0f || height <= 0.0f) return;

    const float fx = std::sin(yaw);
    const float fz = -std::cos(yaw);
    const float rx = std::cos(yaw);
    const float rz = std::sin(yaw);
    const float halfW = width * 0.5f;
    const float halfH = height * 0.5f;
    const float y = pos.y + yOffset;

    const Vec3 right{rx, 0.0f, rz};
    const Vec3 forward{fx, 0.0f, fz};
    const Vec3 southWest = pos - right * halfW + forward * halfH;
    const Vec3 southEast = pos + right * halfW + forward * halfH;
    const Vec3 northEast = pos + right * halfW - forward * halfH;
    const Vec3 northWest = pos - right * halfW - forward * halfH;

    CityVertex vertices[6];
    auto put = [&](int i, const Vec3& p, float u, float v) {
        vertices[i] = makeVertex(Vec3{p.x, y, p.z}, u, v, r, g, b, a);
    };
    put(0, southWest, 0.0f, 1.0f);
    put(1, southEast, 1.0f, 1.0f);
    put(2, northEast, 1.0f, 0.0f);
    put(3, southWest, 0.0f, 1.0f);
    put(4, northEast, 1.0f, 0.0f);
    put(5, northWest, 0.0f, 0.0f);

    Shader& shader = renderer.spriteShader();
    shader.use();
    shader.setMat4("uMVP", camera_.viewProjection());
    shader.setInt("uTexture", 0);
    shader.setInt("uAlphaTest", 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glEnable(GL_BLEND);

    glBindVertexArray(carVao_);
    glBindBuffer(GL_ARRAY_BUFFER, carVbo_);
    const GLsizeiptr bytes = sizeof(vertices);
    if (bytes > carCapacity_) {
        carCapacity_ = bytes;
        glBufferData(GL_ARRAY_BUFFER, bytes, vertices, GL_STREAM_DRAW);
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, vertices);
    }
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Game::drawCar(Renderer& renderer) {
    drawWorldSprite(renderer, assets_.carSpriteTexture(spriteIndex_),
                    vehicle_.position(), vehicle_.yaw(),
                    vehicle_.spriteWidth(), vehicle_.spriteHeight(), 0.03f);
}

int Game::randomCarSprite() {
    const auto& cars = assets_.gry().cars;
    if (cars.empty()) return -1;
    std::uniform_int_distribution<int> pick(0, static_cast<int>(cars.size()) - 1);
    for (int attempt = 0; attempt < 16; ++attempt) {
        const int sprite = assets_.gry().carSpriteIndex(cars[pick(rng_)].sprNum);
        if (sprite >= 0 &&
            sprite < static_cast<int>(assets_.gry().sprites.size()) &&
            assets_.gry().sprites[sprite].w > 0) {
            return sprite;
        }
    }
    return -1;
}

int Game::randomPedSprite() {
    const auto& gry = assets_.gry();
    int base = 0;
    for (int i = 0; i <= 6 && i < 21; ++i) base += gry.spriteNumbers[i];
    std::uniform_int_distribution<int> pick(0, 24);
    for (int attempt = 0; attempt < 16; ++attempt) {
        const int sprite = base + pick(rng_);
        if (sprite < static_cast<int>(gry.sprites.size()) &&
            gry.sprites[sprite].w > 0) {
            return sprite;
        }
    }
    return -1;
}

void Game::spawnTraffic() {
    traffic_.clear();
    const Vec3 player = playerPosition();
    std::uniform_int_distribution<int> coordinate(0, CityMap::kSize - 1);
    static const int dirs[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};

    for (int attempt = 0; attempt < 12000 &&
                            traffic_.size() < 8; ++attempt) {
        const int x = coordinate(rng_);
        const int y = coordinate(rng_);
        const int surface = world_.roadSurface(x, y);
        if (surface <= 0) continue;
        const Vec3 pos{x + 0.5f, static_cast<float>(surface), y + 0.5f};
        const float dx = pos.x - player.x;
        const float dz = pos.z - player.z;
        const float d2 = dx * dx + dz * dz;
        if (d2 < 7.0f * 7.0f || d2 > 35.0f * 35.0f) continue;
        bool tooClose = false;
        for (const TrafficCar& other : traffic_) {
            const float ox = other.pos.x - pos.x;
            const float oz = other.pos.z - pos.z;
            if (ox * ox + oz * oz < 16.0f) {
                tooClose = true;
                break;
            }
        }
        if (tooClose) continue;

        const int sprite = randomCarSprite();
        if (sprite < 0) continue;
        const int widthPixels = assets_.gry().sprites[sprite].w;
        const int heightPixels = assets_.gry().sprites[sprite].h;
        if (widthPixels <= 0 || heightPixels <= 0) continue;
        assets_.spriteTexture(sprite); // upload/cache now

        TrafficCar car;
        car.pos = pos;
        car.cellX = x;
        car.cellY = y;
        car.spriteIndex = sprite;
        car.width = widthPixels / 64.0f;
        car.height = heightPixels / 64.0f;

        int choice = -1;
        int count = 0;
        for (int k = 0; k < 4; ++k) {
            const int s = world_.roadSurface(x + dirs[k][0], y + dirs[k][1]);
            if (s > 0 && std::abs(s - surface) <= 1) {
                ++count;
                if (std::uniform_int_distribution<int>(1, count)(rng_) == 1)
                    choice = k;
            }
        }
        if (choice < 0) {
            continue;
        }
        car.dirX = dirs[choice][0];
        car.dirY = dirs[choice][1];
        car.yaw = std::atan2(static_cast<float>(car.dirX),
                             -static_cast<float>(car.dirY));
        traffic_.push_back(car);
    }
}

void Game::updateTraffic(float dt) {
    if (traffic_.empty()) return;
    static const int dirs[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    const Vec3 player = playerPosition();
    std::uniform_int_distribution<int> coordinate(0, CityMap::kSize - 1);

    auto chooseDirection = [&](TrafficCar& car, bool allowReverse) {
        const int current = world_.roadSurface(car.cellX, car.cellY);
        int options[4];
        int count = 0;
        for (int k = 0; k < 4; ++k) {
            const int dx = dirs[k][0];
            const int dy = dirs[k][1];
            if (!allowReverse && dx == -car.dirX && dy == -car.dirY) continue;
            const int s = world_.roadSurface(car.cellX + dx, car.cellY + dy);
            if (s > 0 && std::abs(s - current) <= 1) {
                options[count++] = k;
            }
        }
        if (count == 0 && !allowReverse) {
            for (int k = 0; k < 4; ++k) {
                const int dx = dirs[k][0];
                const int dy = dirs[k][1];
                const int s = world_.roadSurface(car.cellX + dx, car.cellY + dy);
                if (s > 0 && std::abs(s - current) <= 1) {
                    options[count++] = k;
                }
            }
        }
        if (count == 0) return false;
        std::uniform_int_distribution<int> pick(0, count - 1);
        const int chosen = options[pick(rng_)];
        car.dirX = dirs[chosen][0];
        car.dirY = dirs[chosen][1];
        return true;
    };

    auto respawn = [&](TrafficCar& car) {
        for (int attempt = 0; attempt < 4000; ++attempt) {
            const int x = coordinate(rng_);
            const int y = coordinate(rng_);
            const int surface = world_.roadSurface(x, y);
            if (surface <= 0) continue;
            const Vec3 pos{x + 0.5f, static_cast<float>(surface), y + 0.5f};
            const float dx = pos.x - player.x;
            const float dz = pos.z - player.z;
            const float d2 = dx * dx + dz * dz;
            if (d2 < 20.0f * 20.0f || d2 > 65.0f * 65.0f) continue;
            car.pos = pos;
            car.cellX = x;
            car.cellY = y;
            car.stuck = 0.0f;
            car.health = 100.0f;
            car.dead = false;
            car.respawnTimer = 0.0f;
            if (chooseDirection(car, true)) return;
        }
    };

    for (TrafficCar& car : traffic_) {
        if (car.dead) {
            car.respawnTimer -= dt;
            if (car.respawnTimer <= 0.0f) respawn(car);
            continue;
        }
        const float pdx = car.pos.x - player.x;
        const float pdz = car.pos.z - player.z;
        if (pdx * pdx + pdz * pdz > 90.0f * 90.0f) {
            respawn(car);
            continue;
        }

        const int current = world_.roadSurface(car.cellX, car.cellY);
        int nextSurface =
            world_.roadSurface(car.cellX + car.dirX, car.cellY + car.dirY);
        if (nextSurface <= 0 || std::abs(nextSurface - current) > 1) {
            if (!chooseDirection(car, false)) {
                car.stuck += dt;
                if (car.stuck > 1.0f) respawn(car);
                continue;
            }
            nextSurface =
                world_.roadSurface(car.cellX + car.dirX, car.cellY + car.dirY);
        }

        const Vec3 target{car.cellX + car.dirX + 0.5f,
                          static_cast<float>(nextSurface),
                          car.cellY + car.dirY + 0.5f};
        float dx = target.x - car.pos.x;
        float dz = target.z - car.pos.z;
        float distance = std::sqrt(dx * dx + dz * dz);

        // Keep a safe gap behind the car in front (and behind the player's
        // car) instead of driving through it.
        float speedScale = 1.0f;
        auto blockedBy = [&](const Vec3& other) {
            const float ox = other.x - car.pos.x;
            const float oz = other.z - car.pos.z;
            const float along =
                ox * static_cast<float>(car.dirX) + oz * static_cast<float>(car.dirY);
            const float lateral =
                std::abs(ox * static_cast<float>(car.dirY) -
                         oz * static_cast<float>(car.dirX));
            return along > 0.0f && along < 2.3f && lateral < 0.75f;
        };
        for (const TrafficCar& other : traffic_) {
            if (&other == &car || other.dead) continue;
            if (blockedBy(other.pos)) {
                speedScale = std::min(speedScale, 0.15f);
                break;
            }
        }
        if (inCar_ && blockedBy(vehicle_.position())) {
            speedScale = std::min(speedScale, 0.15f);
        }

        const float step = car.speed * speedScale * dt;
        if (distance <= std::max(step, 0.05f)) {
            car.cellX += car.dirX;
            car.cellY += car.dirY;
            car.pos = target;
            car.stuck = 0.0f;

            const int currentSurf = world_.roadSurface(car.cellX, car.cellY);
            int neighbours = 0;
            for (const auto& dir : dirs) {
                const int s = world_.roadSurface(car.cellX + dir[0],
                                                 car.cellY + dir[1]);
                if (s > 0 && std::abs(s - currentSurf) <= 1) ++neighbours;
            }

            int options[4];
            int count = 0;
            int straight = -1;
            int left = -1;
            int right = -1;
            for (int k = 0; k < 4; ++k) {
                const int stepX = dirs[k][0];
                const int stepY = dirs[k][1];
                if (stepX == -car.dirX && stepY == -car.dirY) continue;
                const int s = world_.roadSurface(car.cellX + stepX,
                                                 car.cellY + stepY);
                if (s <= 0 || std::abs(s - currentSurf) > 1) continue;
                options[count++] = k;
                if (stepX == car.dirX && stepY == car.dirY) straight = k;
                if (stepX == car.dirY && stepY == -car.dirX) left = k;
                if (stepX == -car.dirY && stepY == car.dirX) right = k;
            }

            if (count > 0) {
                int chosen = -1;
                if (neighbours <= 2 && straight >= 0) {
                    // Ordinary street: follow the lane instead of swerving
                    // into every side road.
                    chosen = straight;
                } else if (neighbours >= 3) {
                    // Intersection: mostly straight, sometimes turn.
                    const int roll = std::uniform_int_distribution<int>(0, 99)(rng_);
                    if (straight >= 0 && roll < 52) chosen = straight;
                    else if (left >= 0 && roll < 78) chosen = left;
                    else if (right >= 0) chosen = right;
                    else if (straight >= 0) chosen = straight;
                }
                if (chosen < 0) {
                    chosen = options[std::uniform_int_distribution<int>(
                        0, count - 1)(rng_)];
                }
                car.dirX = dirs[chosen][0];
                car.dirY = dirs[chosen][1];
            } else if (!chooseDirection(car, true)) {
                car.stuck += dt;
            }
        } else if (distance > 0.0001f) {
            car.pos.x += dx / distance * step;
            car.pos.z += dz / distance * step;
            car.pos.y = target.y;
        }

        const float desiredYaw =
            std::atan2(static_cast<float>(car.dirX),
                       -static_cast<float>(car.dirY));
        float yawDiff = desiredYaw - car.yaw;
        while (yawDiff > kPi) yawDiff -= 2.0f * kPi;
        while (yawDiff < -kPi) yawDiff += 2.0f * kPi;
        car.yaw += yawDiff * std::min(1.0f, dt * 12.0f);
    }
}

void Game::drawTraffic(Renderer& renderer) {
    for (const TrafficCar& car : traffic_) {
        if (car.dead) continue;
        const float damage = std::clamp(car.health / 100.0f, 0.0f, 1.0f);
        drawWorldSprite(renderer, assets_.spriteTexture(car.spriteIndex), car.pos,
                        car.yaw, car.width, car.height, 0.02f, 0.55f + 0.45f * damage,
                        0.55f + 0.45f * damage, 0.55f + 0.45f * damage, 1.0f);
    }
}

void Game::spawnPeds() {
    pedestrians_.clear();
    const Vec3 player = playerPosition();
    std::uniform_int_distribution<int> coordinate(0, CityMap::kSize - 1);
    static const int dirs[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};

    for (int attempt = 0; attempt < 12000 &&
                            pedestrians_.size() < 24; ++attempt) {
        const int x = coordinate(rng_);
        const int y = coordinate(rng_);
        // Pedestrians belong on the pavement; only fall back to the road if
        // the area around the player has no walkable pavement at all.
        const bool road = world_.roadSurface(x, y) > 0;
        if (road && attempt < 9000) continue;
        if (!world_.isPassable(x + 0.5f, y + 0.5f)) continue;
        const Vec3 pos{x + 0.5f,
                       static_cast<float>(world_.surfaceHeight(x, y)),
                       y + 0.5f};
        const float dx = pos.x - player.x;
        const float dz = pos.z - player.z;
        const float d2 = dx * dx + dz * dz;
        if (d2 < 3.0f * 3.0f || d2 > 28.0f * 28.0f) continue;

        const int sprite = randomPedSprite();
        if (sprite < 0) continue;
        const int widthPixels = assets_.gry().sprites[sprite].w;
        const int heightPixels = assets_.gry().sprites[sprite].h;
        if (widthPixels <= 0 || heightPixels <= 0) continue;
        assets_.spriteTexture(sprite); // upload/cache now

        Pedestrian ped;
        ped.pos = pos;
        ped.cellX = x;
        ped.cellY = y;
        ped.spriteIndex = sprite;
        ped.width = widthPixels / 64.0f * 1.15f;
        ped.height = heightPixels / 64.0f * 1.15f;
        std::uniform_int_distribution<int> pick(0, 3);
        const int dir = pick(rng_);
        ped.dirX = dirs[dir][0];
        ped.dirY = dirs[dir][1];
        ped.yaw = std::atan2(static_cast<float>(ped.dirX),
                             -static_cast<float>(ped.dirY));
        pedestrians_.push_back(ped);
    }
}

void Game::updatePeds(float dt) {
    if (pedestrians_.empty()) return;
    static const int dirs[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    const Vec3 player = playerPosition();
    std::uniform_int_distribution<int> coordinate(0, CityMap::kSize - 1);

    auto chooseDirection = [&](Pedestrian& ped) {
        int options[4];
        int count = 0;
        int roadOptions[4];
        int roadCount = 0;
        for (int k = 0; k < 4; ++k) {
            const int dx = dirs[k][0];
            const int dy = dirs[k][1];
            if (dx == -ped.dirX && dy == -ped.dirY) continue;
            const int nx = ped.cellX + dx;
            const int ny = ped.cellY + dy;
            if (!world_.isPassable(nx + 0.5f, ny + 0.5f)) continue;
            // Prefer staying on the pavement; road cells are a last resort.
            if (world_.roadSurface(nx, ny) == 0) {
                options[count++] = k;
            } else {
                roadOptions[roadCount++] = k;
            }
        }
        if (count == 0) {
            for (int i = 0; i < roadCount; ++i) options[i] = roadOptions[i];
            count = roadCount;
        }
        if (count == 0) return false;
        std::uniform_int_distribution<int> pick(0, count - 1);
        const int chosen = options[pick(rng_)];
        ped.dirX = dirs[chosen][0];
        ped.dirY = dirs[chosen][1];
        return true;
    };

    auto respawn = [&](Pedestrian& ped) {
        for (int attempt = 0; attempt < 3000; ++attempt) {
            const int x = coordinate(rng_);
            const int y = coordinate(rng_);
            if (world_.roadSurface(x, y) > 0) continue;
            if (!world_.isPassable(x + 0.5f, y + 0.5f)) continue;
            const Vec3 pos{x + 0.5f,
                           static_cast<float>(world_.surfaceHeight(x, y)),
                           y + 0.5f};
            const float dx = pos.x - player.x;
            const float dz = pos.z - player.z;
            const float d2 = dx * dx + dz * dz;
            if (d2 < 12.0f * 12.0f || d2 > 50.0f * 50.0f) continue;
            ped.pos = pos;
            ped.cellX = x;
            ped.cellY = y;
            ped.stuck = 0.0f;
            ped.health = 60.0f;
            ped.dead = false;
            ped.deadTimer = 0.0f;
            ped.panicTimer = 0.0f;
            chooseDirection(ped);
            return;
        }
    };

    for (Pedestrian& ped : pedestrians_) {
        if (ped.dead) {
            ped.deadTimer -= dt;
            if (ped.deadTimer <= 0.0f) respawn(ped);
            continue;
        }
        ped.panicTimer = std::max(0.0f, ped.panicTimer - dt);
        ped.speed = ped.panicTimer > 0.0f ? 2.3f : 0.7f;
        const float pdx = ped.pos.x - player.x;
        const float pdz = ped.pos.z - player.z;
        if (pdx * pdx + pdz * pdz > 70.0f * 70.0f) {
            respawn(ped);
            continue;
        }

        int nextSurface =
            world_.roadSurface(ped.cellX + ped.dirX, ped.cellY + ped.dirY);
        if (!world_.isPassable(ped.cellX + ped.dirX + 0.5f,
                               ped.cellY + ped.dirY + 0.5f)) {
            if (!chooseDirection(ped)) {
                ped.stuck += dt;
                if (ped.stuck > 1.0f) respawn(ped);
                continue;
            }
            nextSurface =
                world_.roadSurface(ped.cellX + ped.dirX, ped.cellY + ped.dirY);
            if (nextSurface <= 0)
                nextSurface = world_.surfaceHeight(ped.cellX + ped.dirX,
                                                   ped.cellY + ped.dirY);
        } else if (nextSurface <= 0) {
            nextSurface = world_.surfaceHeight(ped.cellX + ped.dirX,
                                               ped.cellY + ped.dirY);
        }

        const Vec3 target{ped.cellX + ped.dirX + 0.5f,
                          static_cast<float>(nextSurface),
                          ped.cellY + ped.dirY + 0.5f};
        const float dx = target.x - ped.pos.x;
        const float dz = target.z - ped.pos.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        const float step = ped.speed * dt;
        if (distance <= std::max(step, 0.03f)) {
            ped.cellX += ped.dirX;
            ped.cellY += ped.dirY;
            ped.pos = target;
            ped.stuck = 0.0f;
            if (std::uniform_int_distribution<int>(0, 99)(rng_) < 35) {
                chooseDirection(ped);
            }
        } else if (distance > 0.0001f) {
            ped.pos.x += dx / distance * step;
            ped.pos.z += dz / distance * step;
            ped.pos.y = target.y;
        }
        ped.yaw = std::atan2(static_cast<float>(ped.dirX),
                             -static_cast<float>(ped.dirY));
    }
}

void Game::spawnProps() {
    props_.clear();
    const CityMap& map = assets_.map();
    const GryFile& gry = assets_.gry();
    props_.reserve(map.objects.size());

    for (const MapObject& object : map.objects) {
        const float x = object.x / 64.0f;
        const float z = object.y / 64.0f;
        const float y = 6.05f - object.z / 64.0f;
        if (x < 0.0f || z < 0.0f || x >= 256.0f || z >= 256.0f) continue;
        if (y < -1.0f || y > 20.0f) continue;

        WorldProp prop;
        prop.pos = Vec3{x, y, z};
        prop.yaw = object.rotation * (2.0f * kPi / 1024.0f);

        if (object.remap >= 128) {
            const GryCarInfo* car = nullptr;
            for (const GryCarInfo& info : gry.cars) {
                if (info.model == object.type) {
                    car = &info;
                    break;
                }
            }
            if (!car) continue;
            const int sprite = gry.carSpriteIndex(car->sprNum);
            if (sprite < 0 ||
                sprite >= static_cast<int>(gry.sprites.size()))
                continue;
            prop.spriteIndex = sprite;
            prop.width = gry.sprites[sprite].w / 64.0f;
            prop.height = gry.sprites[sprite].h / 64.0f;
        } else {
            if (object.type == 0 || object.type == 255) continue;
            const int sprite = gry.objectSpriteIndex(object.type);
            if (sprite < 0 ||
                sprite >= static_cast<int>(gry.sprites.size()))
                continue;
            prop.spriteIndex = sprite;
            prop.width = gry.sprites[sprite].w / 64.0f;
            prop.height = gry.sprites[sprite].h / 64.0f;
        }
        if (prop.width <= 0.0f || prop.height <= 0.0f) continue;
        props_.push_back(prop);
    }
}

void Game::drawProps(Renderer& renderer) {
    if (props_.empty()) return;
    const Vec3 center = camera_.target();
    const float maxDistance2 = 42.0f * 42.0f;
    for (const WorldProp& prop : props_) {
        const float dx = prop.pos.x - center.x;
        const float dz = prop.pos.z - center.z;
        if (dx * dx + dz * dz > maxDistance2) continue;
        drawWorldSprite(renderer, assets_.spriteTexture(prop.spriteIndex),
                        prop.pos, prop.yaw, prop.width, prop.height, 0.02f);
    }
}

void Game::drawPeds(Renderer& renderer) {
    for (const Pedestrian& ped : pedestrians_) {
        if (ped.dead) {
            drawWorldSprite(renderer, assets_.spriteTexture(ped.spriteIndex),
                            ped.pos, ped.yaw + kPi * 0.5f, ped.width,
                            ped.height * 0.55f, 0.012f, 0.55f, 0.16f, 0.16f,
                            1.0f);
            continue;
        }
        const float tint = ped.panicTimer > 0.0f ? 1.0f : 0.92f;
        drawWorldSprite(renderer, assets_.spriteTexture(ped.spriteIndex), ped.pos,
                        ped.yaw, ped.width, ped.height, 0.015f, tint, tint,
                        tint * 0.95f, 1.0f);
    }
}

int Game::nearestTrafficCar(float maxDistance) const {
    const Vec3 player = playerPosition();
    int best = -1;
    float bestD2 = maxDistance * maxDistance;
    for (int i = 0; i < static_cast<int>(traffic_.size()); ++i) {
        const TrafficCar& car = traffic_[static_cast<size_t>(i)];
        if (car.dead) continue;
        const float dx = car.pos.x - player.x;
        const float dz = car.pos.z - player.z;
        const float d2 = dx * dx + dz * dz;
        if (d2 < bestD2) {
            bestD2 = d2;
            best = i;
        }
    }
    return best;
}

void Game::carjackNearest(AudioManager& audio) {
    const int index = nearestTrafficCar(2.6f);
    if (index < 0) return;
    const TrafficCar car = traffic_[static_cast<size_t>(index)];
    traffic_.erase(traffic_.begin() + index);

    spriteIndex_ = car.spriteIndex;
    const auto& info = assets_.gry().sprites[car.spriteIndex];
    vehicle_.reset(car.pos);
    vehicle_.setYaw(car.yaw);
    vehicle_.setSprite(car.spriteIndex, info.w, info.h, 0);
    assets_.carSpriteTexture(car.spriteIndex);
    carLabel_ = "STOLEN CAR";
    carHealth_ = std::max(25.0f, car.health);
    carHealthMax_ = 100.0f;
    carSmokeTimer_ = 0.0f;
    lastCarSpeed_ = 0.0f;
    footPos_ = car.pos;
    inCar_ = true;
    camera_.snapTo(car.pos);
    audio.playUiClick();

    bool witnessed = wantedLevel_ > 0;
    for (const PoliceCar& police : policeCars_) {
        if (!police.active) continue;
        const float dx = police.pos.x - car.pos.x;
        const float dz = police.pos.z - car.pos.z;
        if (dx * dx + dz * dz < 16.0f * 16.0f) witnessed = true;
    }
    for (const Cop& cop : cops_) {
        if (!cop.active || cop.dead) continue;
        const float dx = cop.pos.x - car.pos.x;
        const float dz = cop.pos.z - car.pos.z;
        if (dx * dx + dz * dz < 13.0f * 13.0f) witnessed = true;
    }
    if (witnessed) applyWanted(1, "GRAND THEFT AUTO");
    setStatus("CAR JACKED");
}

int Game::weaponIcon() const {
    return 3 + static_cast<int>(weapon_);
}

std::string Game::weaponName() const {
    switch (weapon_) {
    case WeaponType::Pistol:
        return "PISTOL";
    case WeaponType::Uzi:
        return "UZI";
    case WeaponType::Flamethrower:
        return "FLAMETHROWER";
    case WeaponType::Rocket:
        return "ROCKET LAUNCHER";
    default:
        return "FISTS";
    }
}

Vec3 Game::aimDirection() const {
    const Vec3 player = playerPosition();
    const float baseYaw = inCar_ ? vehicle_.yaw() : footYaw_;
    const Vec3 forward{std::sin(baseYaw), 0.0f, -std::cos(baseYaw)};

    Vec3 bestTarget{};
    float bestScore = 0.55f;
    bool found = false;

    auto consider = [&](const Vec3& target) {
        const float dx = target.x - player.x;
        const float dz = target.z - player.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        if (distance < 0.2f || distance > 20.0f) return;
        const float nx = dx / distance;
        const float nz = dz / distance;
        const float dot = nx * forward.x + nz * forward.z;
        if (dot > bestScore) {
            bestScore = dot;
            bestTarget = target;
            found = true;
        }
    };

    for (const Pedestrian& ped : pedestrians_) {
        if (!ped.dead) consider(ped.pos);
    }
    for (const TrafficCar& car : traffic_) {
        if (!car.dead) consider(car.pos);
    }
    for (const PoliceCar& car : policeCars_) {
        if (car.active) consider(car.pos);
    }
    for (const Cop& cop : cops_) {
        if (cop.active && !cop.dead) consider(cop.pos);
    }
    if (missionPed_.active && missionPed_.alive) consider(missionPed_.pos);
    if (missionCar_.active) consider(missionCar_.pos);

    if (!found) return forward;
    Vec3 direction = (bestTarget - player).normalized();
    direction.y = 0.0f;
    return direction;
}

void Game::spawnParticle(const Vec3& pos, const Vec3& velocity, float life,
                         float size, float r, float g, float b, float gravity,
                         float grow, bool additive) {
    if (particles_.size() > 1400) particles_.erase(particles_.begin(),
                                                   particles_.begin() + 200);
    Particle particle;
    particle.pos = pos;
    particle.velocity = velocity;
    particle.life = life;
    particle.maxLife = life;
    particle.size = size;
    particle.r = r;
    particle.g = g;
    particle.b = b;
    particle.gravity = gravity;
    particle.grow = grow;
    particle.additive = additive;
    particles_.push_back(particle);
}

void Game::spawnBlood(const Vec3& pos, int amount) {
    std::uniform_real_distribution<float> angle(0.0f, 2.0f * kPi);
    std::uniform_real_distribution<float> speed(0.4f, 2.2f);
    for (int i = 0; i < amount; ++i) {
        const float a = angle(rng_);
        const float s = speed(rng_);
        spawnParticle(Vec3{pos.x, pos.y + 0.35f, pos.z},
                      Vec3{std::cos(a) * s, 0.8f + speed(rng_) * 0.4f,
                           std::sin(a) * s},
                      0.35f + speed(rng_) * 0.25f, 0.10f + speed(rng_) * 0.04f,
                      0.55f, 0.03f, 0.03f, 5.0f, 0.02f, false);
    }
}

void Game::spawnSparks(const Vec3& pos) {
    std::uniform_real_distribution<float> angle(0.0f, 2.0f * kPi);
    std::uniform_real_distribution<float> speed(1.0f, 4.5f);
    for (int i = 0; i < 10; ++i) {
        const float a = angle(rng_);
        const float s = speed(rng_);
        spawnParticle(Vec3{pos.x, pos.y + 0.35f, pos.z},
                      Vec3{std::cos(a) * s, 1.2f + speed(rng_), std::sin(a) * s},
                      0.25f + speed(rng_) * 0.08f, 0.07f, 1.0f, 0.72f, 0.20f,
                      8.0f, 0.0f, true);
    }
}

void Game::explodeAt(const Vec3& pos, float radius, int damage,
                     AudioManager& audio, bool fromPlayer) {
    audio.playSfx(49, 0.85f);
    std::uniform_real_distribution<float> angle(0.0f, 2.0f * kPi);
    std::uniform_real_distribution<float> spread(0.0f, 1.0f);
    for (int i = 0; i < 30; ++i) {
        const float a = angle(rng_);
        const float horizontal = spread(rng_) * radius * 0.9f;
        const float upward = 0.5f + spread(rng_) * 2.4f;
        spawnParticle(Vec3{pos.x, pos.y + 0.25f, pos.z},
                      Vec3{std::cos(a) * horizontal, upward,
                           std::sin(a) * horizontal},
                      0.5f + spread(rng_) * 0.7f, 0.18f + spread(rng_) * 0.22f,
                      1.0f, 0.35f + spread(rng_) * 0.45f, 0.06f, 4.0f, 0.30f,
                      true);
    }
    for (int i = 0; i < 14; ++i) {
        const float a = angle(rng_);
        spawnParticle(Vec3{pos.x, pos.y + 0.4f, pos.z},
                      Vec3{std::cos(a) * spread(rng_) * radius,
                           0.8f + spread(rng_) * 3.0f,
                           std::sin(a) * spread(rng_) * radius},
                      1.0f + spread(rng_) * 1.2f, 0.24f + spread(rng_) * 0.2f,
                      0.22f, 0.22f, 0.22f, 3.0f, 0.45f, false);
    }

    auto within = [&](const Vec3& other) {
        const float dx = other.x - pos.x;
        const float dz = other.z - pos.z;
        return dx * dx + dz * dz < radius * radius;
    };

    const Vec3 player = playerPosition();
    if (within(player)) {
        const float dx = player.x - pos.x;
        const float dz = player.z - pos.z;
        const float falloff = 1.0f - std::sqrt(dx * dx + dz * dz) / radius;
        damagePlayer(static_cast<int>(damage * std::max(0.2f, falloff)), audio);
    }
    for (int i = 0; i < static_cast<int>(pedestrians_.size()); ++i) {
        if (within(pedestrians_[static_cast<size_t>(i)].pos))
            damagePedestrian(i, damage * 2, audio);
    }
    for (int i = 0; i < static_cast<int>(traffic_.size()); ++i) {
        if (within(traffic_[static_cast<size_t>(i)].pos))
            damageTrafficCar(i, static_cast<int>(damage * 1.5f), audio);
    }
    for (int i = 0; i < static_cast<int>(policeCars_.size()); ++i) {
        if (within(policeCars_[static_cast<size_t>(i)].pos))
            damagePoliceCar(i, static_cast<float>(damage) * 1.5f, audio);
    }
    for (int i = 0; i < static_cast<int>(cops_.size()); ++i) {
        if (within(cops_[static_cast<size_t>(i)].pos))
            damageCop(i, damage * 2, audio);
    }
    if (missionPed_.active && within(missionPed_.pos)) {
        missionPed_.health -= damage * 2.0f;
    }
    if (missionCar_.active && within(missionCar_.pos)) {
        missionCar_.health -= static_cast<int>(damage * 1.5f);
        missionCar_.shake = 0.7f;
    }
    (void)fromPlayer;
}

void Game::damagePlayer(int amount, AudioManager& audio) {
    if (amount <= 0 || wastedTimer_ > 0.0f || bustedTimer_ > 0.0f) return;
    hurtFlash_ = 1.0f;
    int remaining = amount;
    if (playerArmor_ > 0.0f) {
        const float absorbed = std::min(playerArmor_, static_cast<float>(remaining));
        playerArmor_ -= absorbed;
        remaining -= static_cast<int>(absorbed);
    }
    playerHealth_ -= static_cast<float>(remaining);
    spawnBlood(playerPosition(), 5);
    if (playerHealth_ <= 0.0f) {
        playerHealth_ = 0.0f;
        startWasted(audio);
    }
}

void Game::damageCar(float amount, AudioManager& audio) {
    if (carHealth_ <= 0.0f || amount <= 0.0f) return;
    carHealth_ = std::max(0.0f, carHealth_ - amount);
    carSmokeTimer_ = 0.0f;
    if (carHealth_ <= 0.0f) {
        const Vec3 pos = vehicle_.position();
        explodeAt(pos, 3.6f, 70, audio, false);
        if (inCar_) {
            inCar_ = false;
            footPos_ = pos;
            startWasted(audio);
        } else {
            setStatus("YOUR CAR IS WRECKED");
        }
    }
}

void Game::damagePedestrian(int index, int amount, AudioManager& audio) {
    if (index < 0 || index >= static_cast<int>(pedestrians_.size())) return;
    Pedestrian& ped = pedestrians_[static_cast<size_t>(index)];
    if (ped.dead || amount <= 0) return;
    ped.health -= static_cast<float>(amount);
    ped.panicTimer = 5.0f;
    spawnBlood(ped.pos, 4);
    applyWanted(1, "ASSAULT");
    if (ped.health <= 0.0f) {
        ped.dead = true;
        ped.deadTimer = 9.0f;
        ped.health = 0.0f;
        score_ += 25;
        spawnBlood(ped.pos, 16);
        audio.playSfx(37, 0.55f);
    }
}

void Game::damageTrafficCar(int index, int amount, AudioManager& audio) {
    if (index < 0 || index >= static_cast<int>(traffic_.size())) return;
    TrafficCar& car = traffic_[static_cast<size_t>(index)];
    if (car.dead || amount <= 0) return;
    car.health -= static_cast<float>(amount);
    spawnSparks(car.pos);
    applyWanted(1, "VEHICULAR DAMAGE");
    if (car.health <= 0.0f) {
        car.dead = true;
        car.respawnTimer = 10.0f;
        car.health = 0.0f;
        explodeAt(car.pos, 3.0f, 55, audio, true);
        applyWanted(1, "CAR DESTROYED");
    }
}

void Game::damagePoliceCar(int index, float amount, AudioManager& audio) {
    if (index < 0 || index >= static_cast<int>(policeCars_.size())) return;
    PoliceCar& car = policeCars_[static_cast<size_t>(index)];
    if (!car.active || amount <= 0.0f) return;
    car.health -= amount;
    spawnSparks(car.pos);
    applyWanted(2, "POLICE CAR ATTACKED");
    if (car.health <= 0.0f) {
        car.active = false;
        explodeAt(car.pos, 3.4f, 70, audio, true);
        applyWanted(2, "POLICE CAR DESTROYED");
    }
}

void Game::damageCop(int index, int amount, AudioManager& audio) {
    if (index < 0 || index >= static_cast<int>(cops_.size())) return;
    Cop& cop = cops_[static_cast<size_t>(index)];
    if (!cop.active || cop.dead || amount <= 0) return;
    cop.health -= static_cast<float>(amount);
    spawnBlood(cop.pos, 4);
    applyWanted(2, "POLICE OFFICER ATTACKED");
    if (cop.health <= 0.0f) {
        cop.dead = true;
        cop.deadTimer = 12.0f;
        cop.health = 0.0f;
        spawnBlood(cop.pos, 14);
        score_ += 100;
        applyWanted(2, "POLICE OFFICER DOWN");
    }
}

void Game::applyWanted(int stars, const std::string& reason) {
    if (stars <= 0) return;
    const int before = wantedLevel_;
    wantedLevel_ = std::clamp(wantedLevel_ + stars, 0, 5);
    wantedDecay_ = 24.0f;
    wantedFlash_ = 1.0f;
    if (wantedLevel_ > before) {
        policeSpawnTimer_ = std::min(policeSpawnTimer_, 0.8f);
        setStatus(reason + "  WANTED " + std::to_string(wantedLevel_));
    }
}

void Game::clearPolice() {
    policeCars_.clear();
    cops_.clear();
    wantedLevel_ = 0;
    wantedDecay_ = 0.0f;
    wantedFlash_ = 0.0f;
    policeSpawnTimer_ = 2.0f;
    sirenTimer_ = 0.0f;
}

void Game::startWasted(AudioManager& audio) {
    if (wastedTimer_ > 0.0f || bustedTimer_ > 0.0f) return;
    wastedTimer_ = 3.2f;
    deathFade_ = 0.15f;
    hurtFlash_ = 1.0f;
    audio.playSfx(56, 0.8f);
    if (missionState_ == MissionState::Active)
        completeMission(false, audio);
    projectiles_.clear();
}

void Game::startBusted(AudioManager& audio) {
    if (wastedTimer_ > 0.0f || bustedTimer_ > 0.0f) return;
    bustedTimer_ = 2.8f;
    deathFade_ = 0.15f;
    audio.playSfx(53, 0.8f);
    if (missionState_ == MissionState::Active)
        completeMission(false, audio);
    projectiles_.clear();
    clearPolice();
}

void Game::respawnPlayer(AudioManager& audio) {
    (void)audio;
    playerHealth_ = 100.0f;
    playerArmor_ = 0.0f;
    score_ = std::max(0, score_ - 150);
    hurtFlash_ = 0.0f;
    deathFade_ = 0.0f;
    wastedTimer_ = 0.0f;
    bustedTimer_ = 0.0f;
    wantedLevel_ = 0;
    wantedDecay_ = 0.0f;
    policeSpawnTimer_ = 2.0f;
    clearPolice();
    projectiles_.clear();
    particles_.clear();
    resetCar();
    inCar_ = false;
    camera_.snapTo(footPos_);
    setStatus("YOU LOST $150");
}

void Game::spawnWeaponPickups() {
    weaponPickups_.clear();
    const Vec3 spawn = playerPosition();
    std::uniform_int_distribution<int> coordinate(0, CityMap::kSize - 1);

    struct Spec {
        PickupKind kind;
        WeaponType weapon;
        int amount;
        int sprite;
    };
    const Spec specs[] = {
        {PickupKind::Weapon, WeaponType::Pistol, 30, 523},
        {PickupKind::Weapon, WeaponType::Uzi, 60, 530},
        {PickupKind::Weapon, WeaponType::Flamethrower, 80, 547},
        {PickupKind::Weapon, WeaponType::Rocket, 4, 561},
        {PickupKind::Weapon, WeaponType::Uzi, 40, 530},
        {PickupKind::Health, WeaponType::Pistol, 45, 563},
        {PickupKind::Health, WeaponType::Pistol, 45, 563},
        {PickupKind::Health, WeaponType::Pistol, 45, 563},
        {PickupKind::Health, WeaponType::Pistol, 45, 563},
        {PickupKind::Armor, WeaponType::Pistol, 50, 564},
        {PickupKind::Armor, WeaponType::Pistol, 50, 564},
        {PickupKind::Armor, WeaponType::Pistol, 50, 564},
    };

    for (const Spec& spec : specs) {
        for (int attempt = 0; attempt < 6000; ++attempt) {
            const int x = coordinate(rng_);
            const int y = coordinate(rng_);
            const int surface = world_.roadSurface(x, y);
            if (surface <= 0) continue;
            const Vec3 pos{x + 0.5f, static_cast<float>(surface), y + 0.5f};
            const float dx = pos.x - spawn.x;
            const float dz = pos.z - spawn.z;
            const float d2 = dx * dx + dz * dz;
            if (d2 < 8.0f * 8.0f || d2 > 92.0f * 92.0f) continue;
            bool tooClose = false;
            for (const WeaponPickup& other : weaponPickups_) {
                const float ox = other.pos.x - pos.x;
                const float oz = other.pos.z - pos.z;
                if (ox * ox + oz * oz < 5.0f * 5.0f) {
                    tooClose = true;
                    break;
                }
            }
            for (const Phone& phone : phones_) {
                const float ox = phone.pos.x - pos.x;
                const float oz = phone.pos.z - pos.z;
                if (ox * ox + oz * oz < 3.5f * 3.5f) {
                    tooClose = true;
                    break;
                }
            }
            if (tooClose) continue;
            WeaponPickup pickup;
            pickup.pos = pos;
            pickup.kind = spec.kind;
            pickup.weapon = spec.weapon;
            pickup.amount = spec.amount;
            pickup.spriteIndex = spec.sprite;
            assets_.spriteTexture(spec.sprite);
            weaponPickups_.push_back(pickup);
            break;
        }
    }
}

void Game::updateWeaponPickups(float dt, AudioManager& audio) {
    const Vec3 player = playerPosition();
    for (WeaponPickup& pickup : weaponPickups_) {
        if (!pickup.active) {
            pickup.respawnTimer -= dt;
            if (pickup.respawnTimer <= 0.0f) pickup.active = true;
            continue;
        }
        const float dx = pickup.pos.x - player.x;
        const float dz = pickup.pos.z - player.z;
        if (dx * dx + dz * dz > 1.25f * 1.25f) continue;
        if (std::abs(pickup.pos.y - player.y) > 2.0f) continue;

        pickup.active = false;
        pickup.respawnTimer = 26.0f;
        audio.playPickupSound();
        if (pickup.kind == PickupKind::Health) {
            playerHealth_ = std::min(100.0f, playerHealth_ + pickup.amount);
            setStatus("HEALTH +" + std::to_string(pickup.amount));
        } else if (pickup.kind == PickupKind::Armor) {
            playerArmor_ = std::min(100.0f, playerArmor_ + pickup.amount);
            setStatus("ARMOR +" + std::to_string(pickup.amount));
        } else {
            const int weaponIndex = static_cast<int>(pickup.weapon);
            ammo_[weaponIndex] = std::min(9999, ammo_[weaponIndex] + pickup.amount);
            weapon_ = pickup.weapon;
            setStatus(weaponName() + " +" + std::to_string(pickup.amount));
        }
        for (int i = 0; i < 10; ++i) {
            const float a = static_cast<float>(i) * (2.0f * kPi / 10.0f);
            spawnParticle(Vec3{pickup.pos.x, pickup.pos.y + 0.3f, pickup.pos.z},
                          Vec3{std::cos(a) * 0.8f, 1.2f, std::sin(a) * 0.8f},
                          0.45f, 0.07f, 1.0f, 0.92f, 0.45f, 3.0f, 0.02f, true);
        }
    }
}

void Game::fireWeapon(InputManager& input, AudioManager& audio) {
    (void)input;
    if (fireCooldown_ > 0.0f) return;
    const int weaponIndex = static_cast<int>(weapon_);
    const bool empty = ammo_[weaponIndex] <= 0;
    if (empty) {
        // Bare-knuckle swing.
        fireCooldown_ = 0.45f;
        const Vec3 player = playerPosition();
        const Vec3 forward{std::sin(footYaw_), 0.0f, -std::cos(footYaw_)};
        const Vec3 strike = player + forward * 0.9f;
        for (int i = 0; i < static_cast<int>(pedestrians_.size()); ++i) {
            Pedestrian& ped = pedestrians_[static_cast<size_t>(i)];
            if (ped.dead) continue;
            const float dx = ped.pos.x - strike.x;
            const float dz = ped.pos.z - strike.z;
            if (dx * dx + dz * dz < 1.1f * 1.1f) {
                damagePedestrian(i, 34, audio);
                break;
            }
        }
        for (int i = 0; i < static_cast<int>(cops_.size()); ++i) {
            Cop& cop = cops_[static_cast<size_t>(i)];
            if (!cop.active || cop.dead) continue;
            const float dx = cop.pos.x - strike.x;
            const float dz = cop.pos.z - strike.z;
            if (dx * dx + dz * dz < 1.1f * 1.1f) {
                damageCop(i, 28, audio);
                break;
            }
        }
        audio.playSfx(66, 0.5f);
        return;
    }

    --ammo_[weaponIndex];
    const Vec3 player = playerPosition();
    const Vec3 direction = aimDirection();
    const Vec3 muzzle{player.x + direction.x * 0.55f, player.y + 0.72f,
                      player.z + direction.z * 0.55f};
    const float yaw = std::atan2(direction.x, -direction.z);
    footYaw_ = yaw;
    muzzleFlash_ = 0.08f;

    std::uniform_real_distribution<float> spread(-0.055f, 0.055f);
    auto launch = [&](float speed, int damage, bool explosive, bool flame,
                      float life, float radius) {
        Projectile projectile;
        projectile.pos = Vec3{muzzle.x, muzzle.y, muzzle.z};
        const Vec3 dir = Vec3{direction.x + spread(rng_), 0.0f,
                              direction.z + spread(rng_)}.normalized();
        projectile.velocity = dir * speed;
        projectile.life = life;
        projectile.damage = damage;
        projectile.fromPlayer = true;
        projectile.explosive = explosive;
        projectile.flame = flame;
        projectile.radius = radius;
        projectiles_.push_back(projectile);
    };

    switch (weapon_) {
    case WeaponType::Pistol:
        launch(30.0f, 34, false, false, 1.2f, 0.22f);
        audio.playSfx(43, 0.55f);
        fireCooldown_ = 0.30f;
        break;
    case WeaponType::Uzi:
        launch(34.0f, 14, false, false, 1.0f, 0.18f);
        audio.playSfx(59, 0.35f);
        fireCooldown_ = 0.09f;
        break;
    case WeaponType::Flamethrower:
        launch(10.0f, 7, false, true, 0.38f, 0.30f);
        for (int i = 0; i < 5; ++i) {
            spawnParticle(muzzle,
                          Vec3{direction.x * (3.0f + spread(rng_) * 8.0f),
                               spread(rng_) * 0.4f,
                               direction.z * (3.0f + spread(rng_) * 8.0f)},
                          0.28f + spread(rng_) * 0.2f, 0.14f, 1.0f,
                          0.55f + spread(rng_), 0.06f, -1.0f, 0.30f, true);
        }
        audio.playSfx(44, 0.30f);
        fireCooldown_ = 0.055f;
        break;
    case WeaponType::Rocket:
        launch(21.0f, 35, true, false, 2.2f, 0.28f);
        audio.playSfx(53, 0.55f);
        fireCooldown_ = 0.85f;
        break;
    default:
        break;
    }
}

void Game::updateProjectiles(float dt, AudioManager& audio) {
    for (size_t i = 0; i < projectiles_.size();) {
        Projectile& projectile = projectiles_[i];
        projectile.life -= dt;
        projectile.pos.x += projectile.velocity.x * dt;
        projectile.pos.z += projectile.velocity.z * dt;

        bool remove = projectile.life <= 0.0f;
        if (projectile.flame) {
            spawnParticle(projectile.pos,
                          Vec3{projectile.velocity.x * 0.12f, 0.5f,
                               projectile.velocity.z * 0.12f},
                          0.25f, 0.16f, 1.0f, 0.48f, 0.05f, -3.0f, 0.25f,
                          true);
        }
        if (projectile.pos.x < 0.0f || projectile.pos.z < 0.0f ||
            projectile.pos.x > 256.0f || projectile.pos.z > 256.0f) {
            remove = true;
        }
        if (!remove) {
            const int surface = world_.roadSurface(
                static_cast<int>(std::floor(projectile.pos.x)),
                static_cast<int>(std::floor(projectile.pos.z)));
            if (surface <= 0) {
                if (projectile.explosive)
                    explodeAt(projectile.pos, 3.3f, 80, audio, true);
                else
                    spawnSparks(projectile.pos);
                remove = true;
            }
        }

        if (!remove) {
            auto hit = [&](const Vec3& pos, float radius) {
                const float dx = pos.x - projectile.pos.x;
                const float dz = pos.z - projectile.pos.z;
                return dx * dx + dz * dz < radius * radius;
            };

            bool consumed = false;
            if (projectile.fromPlayer) {
                if (missionPed_.active && missionPed_.alive &&
                    hit(missionPed_.pos, 0.55f)) {
                    missionPed_.health -= projectile.damage;
                    spawnBlood(missionPed_.pos, 5);
                    consumed = true;
                }
                if (!consumed && missionCar_.active &&
                    hit(missionCar_.pos, 0.95f)) {
                    missionCar_.health -= projectile.damage;
                    missionCar_.shake = 0.5f;
                    spawnSparks(missionCar_.pos);
                    consumed = true;
                }
                for (int p = 0; p < static_cast<int>(pedestrians_.size()) &&
                                !consumed;
                     ++p) {
                    Pedestrian& ped = pedestrians_[static_cast<size_t>(p)];
                    if (ped.dead || !hit(ped.pos, 0.45f)) continue;
                    damagePedestrian(p, projectile.damage, audio);
                    consumed = true;
                }
                for (int c = 0; c < static_cast<int>(cops_.size()) && !consumed;
                     ++c) {
                    Cop& cop = cops_[static_cast<size_t>(c)];
                    if (!cop.active || cop.dead || !hit(cop.pos, 0.5f)) continue;
                    damageCop(c, projectile.damage, audio);
                    consumed = true;
                }
                for (int c = 0; c < static_cast<int>(policeCars_.size()) &&
                                !consumed;
                     ++c) {
                    PoliceCar& car = policeCars_[static_cast<size_t>(c)];
                    if (!car.active || !hit(car.pos, 1.0f)) continue;
                    damagePoliceCar(c, static_cast<float>(projectile.damage),
                                    audio);
                    consumed = true;
                }
                for (int c = 0; c < static_cast<int>(traffic_.size()) && !consumed;
                     ++c) {
                    TrafficCar& car = traffic_[static_cast<size_t>(c)];
                    if (car.dead || !hit(car.pos, 1.0f)) continue;
                    damageTrafficCar(c, projectile.damage, audio);
                    consumed = true;
                }
            } else {
                if (inCar_ && hit(vehicle_.position(), 1.0f)) {
                    damageCar(static_cast<float>(projectile.damage) * 0.6f,
                              audio);
                    consumed = true;
                } else if (!inCar_ && hit(playerPosition(), 0.55f)) {
                    damagePlayer(projectile.damage, audio);
                    consumed = true;
                }
                if (!consumed && hit(playerPosition(), 1.4f) &&
                    std::abs(projectile.pos.y - playerPosition().y) < 1.8f) {
                    damagePlayer(static_cast<int>(projectile.damage * 0.5f),
                                 audio);
                    consumed = true;
                }
            }

            if (consumed) {
                if (projectile.explosive) {
                    explodeAt(projectile.pos, 3.3f, 80, audio,
                              projectile.fromPlayer);
                } else if (!projectile.flame) {
                    spawnSparks(projectile.pos);
                }
                remove = true;
            }
        }

        if (remove) {
            projectiles_.erase(projectiles_.begin() +
                               static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

void Game::updateParticles(float dt) {
    for (size_t i = 0; i < particles_.size();) {
        Particle& particle = particles_[i];
        particle.life -= dt;
        particle.pos.x += particle.velocity.x * dt;
        particle.pos.z += particle.velocity.z * dt;
        particle.pos.y += particle.velocity.y * dt;
        particle.velocity.y -= particle.gravity * dt;
        particle.size = std::max(0.01f, particle.size + particle.grow * dt);
        if (particle.life <= 0.0f) {
            particles_.erase(particles_.begin() +
                             static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

void Game::updatePolice(float dt, AudioManager& audio) {
    if (wantedLevel_ > 0) {
        sirenTimer_ -= dt;
        if (sirenTimer_ <= 0.0f) {
            audio.playSfx(39, 0.30f);
            sirenTimer_ = 4.7f;
        }
        wantedDecay_ -= dt;
        if (wantedDecay_ <= 0.0f) {
            wantedLevel_ = std::max(0, wantedLevel_ - 1);
            wantedDecay_ = 18.0f;
            setStatus(wantedLevel_ > 0 ? "WANTED LEVEL DECREASED" : "YOU LOST THE COPS");
            if (wantedLevel_ == 0) {
                policeCars_.clear();
                cops_.clear();
            }
        }
    }

    if (wantedLevel_ > 0) {
        policeSpawnTimer_ -= dt;
        const int desired = std::min(wantedLevel_, 3);
        if (policeSpawnTimer_ <= 0.0f &&
            static_cast<int>(policeCars_.size()) < desired) {
            Vec3 spawnPos{};
            if (chooseRoadLocation(playerPosition(), 26.0f, 58.0f, spawnPos)) {
                PoliceCar car;
                car.pos = spawnPos;
                car.active = true;
                car.health = 140.0f;
                car.spriteIndex = 48;
                if (car.spriteIndex >= static_cast<int>(assets_.gry().sprites.size()) ||
                    assets_.gry().sprites[car.spriteIndex].w <= 0) {
                    car.spriteIndex = randomCarSprite();
                }
                if (car.spriteIndex >= 0) {
                    car.yaw = std::atan2(playerPosition().x - spawnPos.x,
                                        -(playerPosition().z - spawnPos.z));
                    assets_.spriteTexture(car.spriteIndex);
                    policeCars_.push_back(car);
                }
            }
            policeSpawnTimer_ = 6.0f;
        }
    }

    const Vec3 player = playerPosition();
    std::uniform_real_distribution<float> jitter(-0.6f, 0.6f);

    for (PoliceCar& car : policeCars_) {
        if (!car.active) continue;
        const float dx = player.x - car.pos.x;
        const float dz = player.z - car.pos.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        if (distance > 95.0f) {
            car.active = false;
            continue;
        }

        const float desired = std::atan2(dx, -dz);
        float difference = desired - car.yaw;
        while (difference > kPi) difference -= 2.0f * kPi;
        while (difference < -kPi) difference += 2.0f * kPi;
        const float turn = std::clamp(difference, -2.4f * dt, 2.4f * dt);
        car.yaw += turn;
        const float targetSpeed = distance > 7.0f ? 10.5f + wantedLevel_ * 0.8f
                                                  : 3.5f;
        car.speed += (targetSpeed - car.speed) * std::min(1.0f, dt * 2.0f);

        const Vec3 next{car.pos.x + std::sin(car.yaw) * car.speed * dt,
                        car.pos.y,
                        car.pos.z - std::cos(car.yaw) * car.speed * dt};
        if (world_.isDrivable(next.x, next.z)) {
            car.pos = next;
            car.stuck = std::max(0.0f, car.stuck - dt);
        } else {
            car.stuck += dt;
            car.speed *= 0.55f;
            car.yaw += (jitter(rng_) > 0.0f ? 1.0f : -1.0f) * 1.8f * dt;
            if (car.stuck > 5.0f) {
                Vec3 respawn{};
                if (chooseRoadLocation(player, 24.0f, 45.0f, respawn)) {
                    car.pos = respawn;
                    car.stuck = 0.0f;
                }
            }
        }
        const int surface =
            world_.roadSurface(static_cast<int>(std::floor(car.pos.x)),
                               static_cast<int>(std::floor(car.pos.z)));
        if (surface > 0) car.pos.y = static_cast<float>(surface);

        car.fireCooldown -= dt;
        if (!inCar_ && distance < 12.0f && car.fireCooldown <= 0.0f) {
            Projectile shot;
            shot.pos = Vec3{car.pos.x, car.pos.y + 0.7f, car.pos.z};
            const Vec3 toPlayer{player.x - car.pos.x, 0.0f,
                                player.z - car.pos.z};
            shot.velocity = toPlayer.normalized() * 24.0f;
            shot.life = 1.2f;
            shot.damage = 11;
            shot.fromPlayer = false;
            shot.radius = 0.18f;
            projectiles_.push_back(shot);
            audio.playSfx(43, 0.22f);
            car.fireCooldown = 1.1f + std::abs(jitter(rng_)) * 0.8f;
        }

        if (wantedLevel_ > 0 && distance < 2.6f) {
            if (inCar_) {
                damageCar(24.0f * dt, audio);
            } else {
                damagePlayer(static_cast<int>(22.0f * dt), audio);
            }
        }

        car.spawnCopTimer -= dt;
        if (!inCar_ && distance < 7.0f && car.spawnCopTimer <= 0.0f &&
            cops_.size() < 3) {
            Cop cop;
            const float side = std::cos(car.yaw) * 1.3f;
            const float sideZ = std::sin(car.yaw) * 1.3f;
            cop.pos = Vec3{car.pos.x + side, car.pos.y, car.pos.z + sideZ};
            if (!world_.isDrivable(cop.pos.x, cop.pos.z)) cop.pos = car.pos;
            cop.active = true;
            cop.health = 55.0f;
            cop.spriteIndex = 811;
            cop.fireCooldown = 0.7f;
            cops_.push_back(cop);
            assets_.spriteTexture(811);
            car.spawnCopTimer = 8.0f;
        }
    }

    for (Cop& cop : cops_) {
        if (!cop.active) continue;
        if (cop.dead) {
            cop.deadTimer -= dt;
            if (cop.deadTimer <= 0.0f) cop.active = false;
            continue;
        }
        const float dx = player.x - cop.pos.x;
        const float dz = player.z - cop.pos.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        if (distance > 70.0f) {
            cop.active = false;
            continue;
        }
        const float desired = std::atan2(dx, -dz);
        float difference = desired - cop.yaw;
        while (difference > kPi) difference -= 2.0f * kPi;
        while (difference < -kPi) difference += 2.0f * kPi;
        cop.yaw += std::clamp(difference, -3.0f * dt, 3.0f * dt);
        cop.animTime += dt;

        if (distance > 1.1f) {
            const Vec3 next{cop.pos.x + std::sin(cop.yaw) * 2.6f * dt,
                            cop.pos.y,
                            cop.pos.z - std::cos(cop.yaw) * 2.6f * dt};
            if (world_.isDrivable(next.x, next.z)) {
                cop.pos = next;
            } else {
                const Vec3 slideX{next.x, cop.pos.y, cop.pos.z};
                const Vec3 slideZ{cop.pos.x, cop.pos.y, next.z};
                if (world_.isDrivable(slideX.x, slideX.z)) cop.pos = slideX;
                else if (world_.isDrivable(slideZ.x, slideZ.z)) cop.pos = slideZ;
            }
            const int surface =
                world_.roadSurface(static_cast<int>(std::floor(cop.pos.x)),
                                   static_cast<int>(std::floor(cop.pos.z)));
            if (surface > 0) cop.pos.y = static_cast<float>(surface);
        }

        int direction = static_cast<int>(std::lround(cop.yaw / (2.0f * kPi) * 8.0f));
        direction = ((direction % 8) + 8) % 8;
        cop.spriteIndex = 811 + direction;

        cop.fireCooldown -= dt;
        if (!inCar_ && distance < 14.0f && cop.fireCooldown <= 0.0f) {
            Projectile shot;
            shot.pos = Vec3{cop.pos.x, cop.pos.y + 0.65f, cop.pos.z};
            const Vec3 toPlayer{player.x - cop.pos.x, 0.0f,
                                player.z - cop.pos.z};
            shot.velocity = toPlayer.normalized() * 25.0f;
            shot.life = 1.2f;
            shot.damage = 9;
            shot.fromPlayer = false;
            shot.radius = 0.16f;
            projectiles_.push_back(shot);
            audio.playSfx(43, 0.20f);
            cop.fireCooldown = 1.0f + std::abs(jitter(rng_)) * 0.7f;
        }
        if (!inCar_ && distance < 1.15f) {
            startBusted(audio);
            return;
        }
    }
}

void Game::updateCombat(float dt, InputManager& input, AudioManager& audio) {
    fireCooldown_ = std::max(0.0f, fireCooldown_ - dt);
    muzzleFlash_ = std::max(0.0f, muzzleFlash_ - dt);
    hurtFlash_ = std::max(0.0f, hurtFlash_ - dt * 1.9f);
    wantedFlash_ = std::max(0.0f, wantedFlash_ - dt * 0.8f);

    const bool modal = missionState_ == MissionState::Briefing ||
                       missionState_ == MissionState::Result;
    if (!modal && !inCar_ && input.down(GameAction::Attack)) {
        fireWeapon(input, audio);
    }

    updateProjectiles(dt, audio);
    updateParticles(dt);
    updateWeaponPickups(dt, audio);
    updatePolice(dt, audio);

    if (inCar_ && carHealth_ <= 35.0f) {
        carSmokeTimer_ -= dt;
        if (carSmokeTimer_ <= 0.0f) {
            const Vec3 pos = vehicle_.position();
            spawnParticle(Vec3{pos.x, pos.y + 0.4f, pos.z},
                          Vec3{(std::uniform_real_distribution<float>(-0.4f, 0.4f))(rng_),
                               1.1f,
                               (std::uniform_real_distribution<float>(-0.4f, 0.4f))(rng_)},
                          1.1f, 0.15f, 0.20f, 0.20f, 0.20f, -0.4f, 0.45f,
                          false);
            carSmokeTimer_ = carHealth_ < 15.0f ? 0.07f : 0.22f;
        }
        if (carHealth_ < 15.0f) {
            const Vec3 pos = vehicle_.position();
            spawnParticle(Vec3{pos.x, pos.y + 0.5f, pos.z},
                          Vec3{0.0f, 1.5f, 0.0f}, 0.5f, 0.12f, 1.0f, 0.55f,
                          0.08f, -2.0f, 0.2f, true);
        }
    }
}

void Game::drawWorldBillboard(Renderer& renderer, GLuint texture, const Vec3& pos,
                              float size, float r, float g, float b, float a) {
    if (!texture || size <= 0.0f) return;
    const Mat4 view = camera_.view();
    const Vec3 right{view.m[0], view.m[4], view.m[8]};
    const Vec3 up{view.m[1], view.m[5], view.m[9]};
    const Vec3 bottomLeft = pos - right * size - up * size;
    const Vec3 bottomRight = pos + right * size - up * size;
    const Vec3 topRight = pos + right * size + up * size;
    const Vec3 topLeft = pos - right * size + up * size;

    CityVertex vertices[6];
    auto put = [&](int i, const Vec3& p, float u, float v) {
        vertices[i] = makeVertex(p, u, v, r, g, b, a);
    };
    put(0, bottomLeft, 0, 1);
    put(1, bottomRight, 1, 1);
    put(2, topRight, 1, 0);
    put(3, bottomLeft, 0, 1);
    put(4, topRight, 1, 0);
    put(5, topLeft, 0, 0);

    Shader& shader = renderer.spriteShader();
    shader.use();
    shader.setMat4("uMVP", camera_.viewProjection());
    shader.setInt("uTexture", 0);
    shader.setInt("uAlphaTest", 0);
    shader.setInt("uMask", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glBindVertexArray(quadVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDepthMask(GL_TRUE);
}

void Game::drawProjectiles(Renderer& renderer) {
    if (!renderer.whiteTexture().valid()) return;
    const GLuint white = renderer.whiteTexture().id();
    for (const Projectile& projectile : projectiles_) {
        if (projectile.flame) {
            drawWorldBillboard(renderer, white, projectile.pos, 0.18f, 1.0f,
                               0.55f, 0.08f, 0.9f);
        } else if (projectile.explosive) {
            drawWorldBillboard(renderer, white, projectile.pos, 0.16f, 1.0f,
                               0.92f, 0.4f, 1.0f);
        } else {
            const float r = projectile.fromPlayer ? 1.0f : 1.0f;
            const float g = projectile.fromPlayer ? 0.95f : 0.3f;
            const float b = projectile.fromPlayer ? 0.55f : 0.2f;
            drawWorldBillboard(renderer, white, projectile.pos, 0.09f, r, g, b,
                               0.95f);
            drawWorldBillboard(renderer, white,
                               projectile.pos - projectile.velocity * 0.018f,
                               0.055f, r, g, b, 0.55f);
        }
    }
}

void Game::drawParticles(Renderer& renderer) {
    if (!renderer.whiteTexture().valid()) return;
    const GLuint white = renderer.whiteTexture().id();
    for (const Particle& particle : particles_) {
        const float alpha =
            std::clamp(particle.life / std::max(0.001f, particle.maxLife), 0.0f,
                       1.0f);
        drawWorldBillboard(renderer, white, particle.pos, particle.size,
                           particle.r, particle.g, particle.b, alpha);
    }
}

void Game::drawWeaponPickups(Renderer& renderer) {
    for (const WeaponPickup& pickup : weaponPickups_) {
        if (!pickup.active) continue;
        const auto& sprites = assets_.gry().sprites;
        if (pickup.spriteIndex < 0 ||
            pickup.spriteIndex >= static_cast<int>(sprites.size()))
            continue;
        const int w = sprites[pickup.spriteIndex].w;
        const int h = sprites[pickup.spriteIndex].h;
        if (w <= 0 || h <= 0) continue;
        const float bob =
            std::sin(worldTime_ * 3.2f + pickup.pos.x) * 0.08f;
        const float scale = 1.45f;
        float r = 1.0f, g = 1.0f, b = 1.0f;
        if (pickup.kind == PickupKind::Health) {
            r = 0.6f;
            g = 1.0f;
            b = 0.65f;
        } else if (pickup.kind == PickupKind::Armor) {
            r = 0.55f;
            g = 0.8f;
            b = 1.0f;
        }
        drawWorldSprite(renderer, assets_.spriteTexture(pickup.spriteIndex),
                        Vec3{pickup.pos.x, pickup.pos.y + bob, pickup.pos.z},
                        worldTime_ * 1.6f + pickup.pos.z,
                        w / 64.0f * scale, h / 64.0f * scale, 0.20f, r, g, b,
                        1.0f);
    }
}

void Game::drawPolice(Renderer& renderer) {
    if (!renderer.whiteTexture().valid()) return;
    const GLuint white = renderer.whiteTexture().id();
    for (const PoliceCar& car : policeCars_) {
        if (!car.active) continue;
        const auto& sprites = assets_.gry().sprites;
        if (car.spriteIndex < 0 ||
            car.spriteIndex >= static_cast<int>(sprites.size()))
            continue;
        const float damage = std::clamp(car.health / 140.0f, 0.0f, 1.0f);
        drawWorldSprite(renderer, assets_.spriteTexture(car.spriteIndex), car.pos,
                        car.yaw, sprites[car.spriteIndex].w / 64.0f,
                        sprites[car.spriteIndex].h / 64.0f, 0.025f,
                        0.55f + 0.45f * damage, 0.55f + 0.45f * damage,
                        0.55f + 0.45f * damage, 1.0f);
        const float flash = std::sin(worldTime_ * 12.0f);
        const float lightY = car.pos.y + 0.55f;
        drawWorldBillboard(renderer, white,
                           Vec3{car.pos.x, lightY, car.pos.z}, 0.13f,
                           flash > 0.0f ? 1.0f : 0.1f,
                           flash > 0.0f ? 0.1f : 0.2f,
                           flash > 0.0f ? 0.1f : 1.0f, 1.0f);
    }
}

void Game::drawCops(Renderer& renderer) {
    for (const Cop& cop : cops_) {
        if (!cop.active) continue;
        const auto& sprites = assets_.gry().sprites;
        if (cop.spriteIndex < 0 ||
            cop.spriteIndex >= static_cast<int>(sprites.size()))
            continue;
        float r = 1.0f, g = 1.0f, b = 1.0f;
        if (cop.dead) {
            r = 0.55f;
            g = 0.18f;
            b = 0.18f;
        }
        drawWorldSprite(renderer, assets_.spriteTexture(cop.spriteIndex), cop.pos,
                        cop.yaw, sprites[cop.spriteIndex].w / 64.0f * 1.35f,
                        sprites[cop.spriteIndex].h / 64.0f * 1.35f, 0.025f, r,
                        g, b, 1.0f);
    }
}

void Game::drawPickups(Renderer& renderer) {
    if (pickups_.empty() || !renderer.whiteTexture().valid()) return;

    Shader& shader = renderer.spriteShader();
    shader.use();
    shader.setMat4("uMVP", camera_.viewProjection());
    shader.setInt("uTexture", 0);
    shader.setInt("uAlphaTest", 0);
    renderer.whiteTexture().bind(0);
    glEnable(GL_BLEND);

    glBindVertexArray(carVao_);
    glBindBuffer(GL_ARRAY_BUFFER, carVbo_);
    for (const Pickup& pickup : pickups_) {
        if (!pickup.active) continue;
        const float bob =
            std::sin(worldTime_ * 3.0f + pickup.pos.x + pickup.pos.z) * 0.06f;
        const Vec3 center{pickup.pos.x, pickup.pos.y + 0.45f + bob, pickup.pos.z};
        const float angle = worldTime_ * 2.2f + pickup.pos.x;
        const float size = 0.32f;
        const Vec3 right{std::cos(angle) * size, 0.0f, std::sin(angle) * size};
        const Vec3 forward{std::sin(angle) * size, 0.0f, -std::cos(angle) * size};
        const Vec3 a = center - right - forward;
        const Vec3 b = center + right - forward;
        const Vec3 c = center + right + forward;
        const Vec3 d = center - right + forward;

        CityVertex v[6];
        v[0] = makeVertex(a, 0, 1, 1.0f, 0.82f, 0.18f, 0.95f);
        v[1] = makeVertex(b, 1, 1, 1.0f, 0.82f, 0.18f, 0.95f);
        v[2] = makeVertex(c, 1, 0, 1.0f, 0.82f, 0.18f, 0.95f);
        v[3] = makeVertex(a, 0, 1, 1.0f, 0.82f, 0.18f, 0.95f);
        v[4] = makeVertex(c, 1, 0, 1.0f, 0.82f, 0.18f, 0.95f);
        v[5] = makeVertex(d, 0, 0, 1.0f, 0.82f, 0.18f, 0.95f);
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

Game::MenuFrame Game::drawOriginalBackdrop(Renderer& renderer, bool showLogo) {
    MenuFrame frame;
    const float w = static_cast<float>(renderer.width());
    const float h = static_cast<float>(renderer.height());
    frame.scale = std::min(w / 640.0f, h / 480.0f);
    frame.offsetX = (w - 640.0f * frame.scale) * 0.5f;
    frame.offsetY = (h - 480.0f * frame.scale) * 0.5f;

    drawRect(renderer, 0, 0, w, h, 0, 0, 0, 1.0f);
    if (!originalAssets_) {
        drawRect(renderer, 0, 0, w, h, 0, 0, 0, 0.62f);
        return frame;
    }

    drawTexturedRect(renderer, menuUpper_, frame.offsetX, frame.offsetY,
                     640.0f * frame.scale, 168.0f * frame.scale, 0, 0, 1, 1,
                     1, 1, 1, 1);
    drawTexturedRect(renderer, menuLower_, frame.offsetX,
                     frame.offsetY + 168.0f * frame.scale,
                     640.0f * frame.scale, 312.0f * frame.scale, 0, 0, 1, 1,
                     1, 1, 1, 1);
    if (showLogo) {
        const int logoFrame =
            std::clamp(static_cast<int>(menuTime_ * 10.0f), 0, 7);
        if (logoTextures_[logoFrame]) {
            drawTexturedRect(renderer, logoTextures_[logoFrame], frame.offsetX,
                             frame.offsetY, 640.0f * frame.scale,
                             168.0f * frame.scale, 0, 0, 1, 1, 1, 1, 1, 1);
        }
    }
    return frame;
}

void Game::drawOriginalText(Renderer& renderer, const MenuFrame& frame,
                            const std::string& text, float x, float y,
                            float scale, float r, float g, float b, float a,
                            bool centered) {
    const float px = frame.offsetX + x * frame.scale;
    const float py = frame.offsetY + y * frame.scale;
    const float realScale = scale * frame.scale;
    if (menuFont_.valid()) {
        float drawX = px;
        if (centered) drawX -= menuFont_.measure(text, realScale) * 0.5f;
        menuFont_.draw(renderer.spriteShader(), text, drawX, py, realScale, r, g,
                       b, a, renderer.width(), renderer.height());
    } else {
        drawText(renderer, text, px, py, 5.0f * realScale, r, g, b, a,
                 centered);
    }
}

void Game::drawMissionTargets(Renderer& renderer) {
    if (missionCar_.active) {
        const float shakeX =
            std::sin(worldTime_ * 62.0f) * 0.06f * missionCar_.shake;
        const float shakeZ =
            std::cos(worldTime_ * 51.0f) * 0.06f * missionCar_.shake;
        const Vec3 pos{missionCar_.pos.x + shakeX, missionCar_.pos.y,
                       missionCar_.pos.z + shakeZ};
        float r = 1.0f, g = 1.0f, b = 1.0f;
        if (missionType_ == MissionType::DestroyCar) {
            const float health =
                std::clamp(missionCar_.health / 120.0f, 0.0f, 1.0f);
            r = 1.0f;
            g = 0.55f + 0.45f * health;
            b = 0.45f + 0.55f * health;
        }
        drawWorldSprite(renderer,
                        assets_.spriteTexture(missionCar_.spriteIndex), pos,
                        missionCar_.yaw, missionCar_.width,
                        missionCar_.height, 0.03f, r, g, b, 1.0f);
    }
    if (missionPed_.active && missionPed_.alive) {
        float r = 1.0f, g = 1.0f, b = 1.0f;
        if (missionType_ == MissionType::KillPed) {
            r = 1.0f;
            g = 0.55f;
            b = 0.55f;
        }
        drawWorldSprite(renderer,
                        assets_.spriteTexture(missionPed_.spriteIndex),
                        missionPed_.pos, missionPed_.yaw, missionPed_.width,
                        missionPed_.height, 0.035f, r, g, b, 1.0f);
    }
}

void Game::drawMissionMarkers(Renderer& renderer) {
    if (!renderer.whiteTexture().valid()) return;

    Shader& shader = renderer.spriteShader();
    shader.use();
    shader.setMat4("uMVP", camera_.viewProjection());
    shader.setInt("uTexture", 0);
    shader.setInt("uAlphaTest", 0);
    shader.setInt("uMask", 0);
    renderer.whiteTexture().bind(0);
    glEnable(GL_BLEND);
    glBindVertexArray(carVao_);
    glBindBuffer(GL_ARRAY_BUFFER, carVbo_);

    auto diamond = [&](const Vec3& center, float size, float angle, float r,
                       float g, float b, float a) {
        const Vec3 right{std::cos(angle) * size, 0.0f, std::sin(angle) * size};
        const Vec3 forward{std::sin(angle) * size, 0.0f, -std::cos(angle) * size};
        const Vec3 p0 = center - right - forward;
        const Vec3 p1 = center + right - forward;
        const Vec3 p2 = center + right + forward;
        const Vec3 p3 = center - right + forward;
        CityVertex v[6];
        v[0] = makeVertex(p0, 0, 1, r, g, b, a);
        v[1] = makeVertex(p1, 1, 1, r, g, b, a);
        v[2] = makeVertex(p2, 1, 0, r, g, b, a);
        v[3] = makeVertex(p0, 0, 1, r, g, b, a);
        v[4] = makeVertex(p2, 1, 0, r, g, b, a);
        v[5] = makeVertex(p3, 0, 0, r, g, b, a);
        glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    };

    if (missionState_ == MissionState::Idle && !phones_.empty()) {
        const Vec3& phone = phones_[static_cast<size_t>(activePhone_)].pos;
        const float pulse = 0.34f + 0.06f * std::sin(worldTime_ * 4.0f);
        const Vec3 center{phone.x, phone.y + 0.45f, phone.z};
        diamond(center, pulse * 1.6f, worldTime_ * 0.7f, 0.15f, 1.0f, 0.75f,
                0.16f);
        diamond(center, pulse, worldTime_ * 1.6f, 0.45f, 1.0f, 0.9f, 0.95f);
    }

    if (missionState_ == MissionState::Active) {
        const float pulse = 0.4f + 0.07f * std::sin(worldTime_ * 5.0f);
        Vec3 center{targetPos_.x, targetPos_.y + 0.55f, targetPos_.z};
        if (missionCar_.active &&
            missionPhase_ == MissionPhase::GoToTarget) {
            center = Vec3{missionCar_.pos.x, missionCar_.pos.y + 0.6f,
                          missionCar_.pos.z};
        } else if (missionPed_.active && missionPed_.alive &&
                   missionPhase_ == MissionPhase::GoToTarget) {
            center = Vec3{missionPed_.pos.x, missionPed_.pos.y + 0.6f,
                          missionPed_.pos.z};
        }
        const bool hostile = missionType_ == MissionType::KillPed ||
                             missionType_ == MissionType::DestroyCar;
        const float r = hostile ? 1.0f : 1.0f;
        const float g = hostile ? 0.22f : 0.86f;
        const float b = hostile ? 0.18f : 0.2f;
        diamond(center, pulse * 1.8f, worldTime_, r, g, b, 0.18f);
        diamond(center, pulse, -worldTime_ * 1.4f, r, g, b, 0.95f);
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

std::string Game::wrapTextWidth(const std::string& text, float maxWidth,
                                float scale, const BitmapFont& font) const {
    std::string result;
    std::string line;
    std::string word;
    auto measure = [&](const std::string& value) {
        if (font.valid()) return font.measure(value, scale);
        return static_cast<float>(value.size()) * 6.0f;
    };
    auto flush = [&]() {
        if (word.empty()) return;
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && measure(candidate) > maxWidth) {
            result += line;
            result += '\n';
            line = word;
        } else {
            line = candidate;
        }
        word.clear();
    };
    for (char c : text) {
        if (c == ' ') {
            flush();
        } else if (c == '\n') {
            flush();
            result += line;
            result += '\n';
            line.clear();
        } else {
            word += c;
        }
    }
    flush();
    if (!line.empty()) result += line;
    return result;
}

std::string Game::wrapText(const std::string& text, size_t width) const {
    std::string result;
    std::string line;
    std::string word;
    auto flushWord = [&]() {
        if (word.empty()) return;
        if (!line.empty() && line.size() + 1 + word.size() > width) {
            result += line;
            result += '\n';
            line.clear();
        }
        if (!line.empty()) line += ' ';
        line += word;
        word.clear();
    };
    for (char c : text) {
        if (c == ' ' || c == '\n') {
            flushWord();
            if (c == '\n') {
                result += line;
                result += '\n';
                line.clear();
            }
        } else {
            word += c;
        }
    }
    flushWord();
    if (!line.empty()) result += line;
    return result;
}

void Game::drawMissionUi(Renderer& renderer) {
    const float w = static_cast<float>(renderer.width());
    const float h = static_cast<float>(renderer.height());
    const float s = uiScale();

    if (missionState_ == MissionState::Briefing) {
        const char* objective = "GET TO THE MARKER";
        switch (missionType_) {
        case MissionType::StealDeliver:
            objective = "STEAL A CAR AND DELIVER IT";
            break;
        case MissionType::Pickup:
            objective = "PICK UP A CONTACT AND DRIVE";
            break;
        case MissionType::KillPed:
            objective = "WASTE THE TARGET";
            break;
        case MissionType::DestroyCar:
            objective = "DESTROY THE TARGET CAR";
            break;
        case MissionType::Reach:
        default:
            objective = "GET TO THE MARKER";
            break;
        }
        drawRect(renderer, 0, 0, w, h, 0, 0, 0, 0.66f);
        const float panelW = std::min(w * 0.78f, 780.0f * s);
        const float bodyScale =
            std::clamp(0.72f * s, 0.45f, 1.6f);
        const float pad = 34.0f * s;
        const float maxWidth = panelW - pad * 2.0f;
        const std::string body =
            wrapTextWidth(briefingText_, maxWidth, bodyScale, missionFont_);
        int bodyLines = 1;
        for (char c : body) {
            if (c == '\n') ++bodyLines;
        }
        const float lineStep =
            (missionFont_.valid() ? missionFont_.height() * bodyScale
                                  : 18.0f * s) +
            7.0f * s;
        const float headerH = 52.0f * s;
        float panelH = headerH + 52.0f * s + bodyLines * lineStep + 96.0f * s;
        panelH = std::clamp(panelH, 250.0f * s, h * 0.84f);
        const float x = (w - panelW) * 0.5f;
        const float y = (h - panelH) * 0.5f;
        drawRect(renderer, x + 5.0f * s, y + 6.0f * s, panelW, panelH, 0.0f,
                 0.0f, 0.0f, 0.45f);
        drawRect(renderer, x, y, panelW, panelH, 0.015f, 0.02f, 0.018f, 0.96f);
        drawRect(renderer, x, y, panelW, 2.0f * s, 0.9f, 0.76f, 0.28f, 1.0f);
        drawRect(renderer, x, y + panelH - 2.0f * s, panelW, 2.0f * s, 0.9f,
                 0.76f, 0.28f, 1.0f);
        drawRect(renderer, x, y, 2.0f * s, panelH, 0.9f, 0.76f, 0.28f, 1.0f);
        drawRect(renderer, x + panelW - 2.0f * s, y, 2.0f * s, panelH, 0.9f,
                 0.76f, 0.28f, 1.0f);

        drawRect(renderer, x, y, panelW, headerH, 0.06f, 0.12f, 0.06f, 1.0f);
        char header[64];
        std::snprintf(header, sizeof(header), "JOB %02d OF %02d",
                      std::min(missionIndex_ + 1,
                               static_cast<int>(missions_.size())),
                      static_cast<int>(missions_.size()));
        drawHudText(renderer, missionHeaderFont_, "MISSION BRIEFING",
                    x + 24.0f * s, y + 5.0f * s, 0.55f * s, 1.0f, 0.9f, 0.55f,
                    1.0f, false, true);
        drawHudText(renderer, missionFont_, header, x + panelW - 24.0f * s,
                    y + 17.0f * s, 0.45f * s, 0.75f, 0.85f, 0.7f, 0.95f, false,
                    true);
        drawHudText(renderer, missionFont_, std::string("OBJECTIVE   ") + objective,
                    x + pad, y + headerH + 12.0f * s, 0.5f * s, 1.0f, 0.82f,
                    0.32f, 1.0f, false, true);

        float lineY = y + headerH + 46.0f * s;
        if (missionFont_.valid()) {
            size_t start = 0;
            while (start <= body.size()) {
                const size_t end = body.find('\n', start);
                const std::string line =
                    body.substr(start, end == std::string::npos
                                           ? std::string::npos
                                           : end - start);
                drawHudText(renderer, missionFont_, line, x + pad, lineY,
                            bodyScale, 1.0f, 0.94f, 0.78f, 1.0f, false, true);
                lineY += lineStep;
                if (end == std::string::npos) break;
                start = end + 1;
            }
        } else {
            drawText(renderer, body, x + pad, lineY, 1.6f * s, 1.0f, 0.94f,
                     0.78f, 1.0f, false);
        }

        const float footerY = y + panelH - 38.0f * s;
        drawRect(renderer, x, footerY - 8.0f * s, panelW, 1.0f * s, 0.4f, 0.4f,
                 0.32f, 0.8f);
        char pay[96];
        std::snprintf(pay, sizeof(pay), "PAY  $%d          TIME  %s",
                      missionReward_, formatTime(missionTimeLimit_).c_str());
        drawHudText(renderer, missionFont_, pay, x + pad, footerY,
                    0.46f * s, 1.0f, 0.84f, 0.35f, 1.0f, false, true);
        const std::string controls = "ENTER / A  ACCEPT     ESC / B  DECLINE";
        const float controlsScale = 0.42f * s;
        const float controlsWidth =
            missionFont_.valid()
                ? missionFont_.measure(controls, controlsScale)
                : textWidth(controls, controlsScale);
        drawHudText(renderer, missionFont_, controls,
                    x + panelW - pad - controlsWidth, footerY, controlsScale,
                    0.8f, 0.85f, 0.9f, 0.95f, false, true);
        return;
    }

    if (missionState_ == MissionState::Result) {
        drawRect(renderer, 0, 0, w, h, 0, 0, 0, 0.45f);
        const bool success = resultText_.find("COMPLETE") != std::string::npos;
        const float scale = 0.9f * s;
        drawHudText(renderer, menuFont_, resultText_, w * 0.5f, h * 0.4f, scale,
                    success ? 0.65f : 1.0f, success ? 1.0f : 0.35f,
                    success ? 0.45f : 0.25f, 1.0f, true, true);
        drawHudText(renderer, missionFont_, "PRESS ENTER TO CONTINUE", w * 0.5f,
                    h * 0.52f, 0.5f * s, 0.85f, 0.9f, 0.95f, 0.95f, true,
                    true);
        return;
    }
}

void Game::renderIntro(Renderer& renderer) {
    const float w = static_cast<float>(renderer.width());
    const float h = static_cast<float>(renderer.height());
    const float s = uiScale();
    drawRect(renderer, 0, 0, w, h, 0, 0, 0, 1.0f);

    if (introTime_ < 4.4f) {
        const bool dma = introTime_ < 2.2f;
        const float local = dma ? introTime_ : introTime_ - 2.2f;
        const float fade = std::max(
            0.0f, std::min(std::min(1.0f, local / 0.35f),
                           std::min(1.0f, (2.2f - local) / 0.45f)));
        const GLuint logo = dma ? dmaLogo_ : bmgLogo_;
        if (logo) {
            const float logoHeight = h * (dma ? 0.56f : 0.42f);
            const float logoWidth =
                logoHeight * (dma ? (78.0f / 109.0f) : (50.0f / 100.0f));
            drawTexturedRect(renderer, logo, (w - logoWidth) * 0.5f,
                             (h - logoHeight) * 0.5f, logoWidth, logoHeight,
                             0, 0, 1, 1, 1.0f, 1.0f, 1.0f, fade);
        } else if (cutTextFont_.valid()) {
            drawHudText(renderer, cutTextFont_,
                        dma ? "DMA DESIGN" : "BMG INTERACTIVE", w * 0.5f,
                        h * 0.45f, 2.2f * s, 1.0f, 0.9f, 0.5f, fade, true,
                        true);
        }
        drawHudText(renderer, cutTextFont_,
                    dma ? "DMA DESIGN LTD" : "BMG INTERACTIVE", w * 0.5f,
                    h * 0.86f, 1.0f * s, 0.72f, 0.76f, 0.82f, fade * 0.9f,
                    true, true);
    }

    drawText(renderer, "v0.2.0", w - 18.0f * s, h - 22.0f * s, 0.8f * s,
             0.45f, 0.5f, 0.58f, 0.75f, false);
}

void Game::renderCutscene(Renderer& renderer) {
    const float w = static_cast<float>(renderer.width());
    const float h = static_cast<float>(renderer.height());
    const float s = uiScale();
    drawRect(renderer, 0, 0, w, h, 0, 0, 0, 1.0f);

    MenuFrame frame;
    frame.scale = std::min(w / 640.0f, h / 480.0f);
    frame.offsetX = (w - 640.0f * frame.scale) * 0.5f;
    frame.offsetY = (h - 480.0f * frame.scale) * 0.5f;

    if (cutsceneTextures_[cutsceneIndex_]) {
        float alpha = 1.0f;
        if (cutsceneLine_ == 0) {
            alpha = std::min(1.0f, cutsceneLineTimer_ / 0.5f + 0.25f);
        }
        drawTexturedRect(renderer, cutsceneTextures_[cutsceneIndex_],
                         frame.offsetX, frame.offsetY, 640.0f * frame.scale,
                         480.0f * frame.scale, 0, 0, 1, 1, 1, 1, 1, alpha);
    }

    const std::string key =
        "cut" + std::to_string(cutsceneIndex_) + std::to_string(cutsceneLine_);
    const auto it = fxt_.find(key);
    if (it != fxt_.end() && !it->second.empty()) {
        const float textScale = 1.0f * frame.scale;
        const float maxWidth = 600.0f * frame.scale;
        const std::string wrapped =
            cutTextFont_.valid()
                ? wrapTextWidth(it->second, maxWidth, textScale, cutTextFont_)
                : it->second;
        int lineCount = 1;
        for (char c : wrapped) {
            if (c == '\n') ++lineCount;
        }
        float y = frame.offsetY + 440.0f * frame.scale -
                  (lineCount - 1) * 34.0f * frame.scale;
        if (cutTextFont_.valid()) {
            // Draw the wrapped block line by line so it stays centred.
            size_t start = 0;
            while (start <= wrapped.size()) {
                const size_t end = wrapped.find('\n', start);
                const std::string line =
                    wrapped.substr(start, end == std::string::npos
                                             ? std::string::npos
                                             : end - start);
                drawHudText(renderer, cutTextFont_, line, w * 0.5f, y,
                            textScale, 1.0f, 0.86f, 0.25f, 1.0f, true, true);
                y += 34.0f * frame.scale;
                if (end == std::string::npos) break;
                start = end + 1;
            }
        } else {
            drawText(renderer, wrapped, w * 0.5f, y, 1.7f * s, 1.0f, 0.86f,
                     0.25f, 1.0f, true);
        }
    }

    if (cutsceneLine_ == 0 || std::fmod(cutsceneLineTimer_, 1.4f) < 0.9f) {
        const float hintScale = 0.55f * s;
        const float hintWidth = cutTextFont_.valid()
                                    ? cutTextFont_.measure("ENTER - CONTINUE",
                                                           hintScale)
                                    : textWidth("ENTER - CONTINUE", hintScale);
        drawHudText(renderer, cutTextFont_, "ENTER - CONTINUE",
                    frame.offsetX + 640.0f * frame.scale - 18.0f * s - hintWidth,
                    frame.offsetY + 480.0f * frame.scale - 34.0f * s,
                    hintScale, 0.8f, 0.82f, 0.88f, 0.8f, false, true);
    }
}

void Game::renderMainMenu(Renderer& renderer) {
    const float w = static_cast<float>(renderer.width());
    const float h = static_cast<float>(renderer.height());
    const float s = uiScale();

    if (originalAssets_) {
        const MenuFrame frame = drawOriginalBackdrop(renderer, true);
        const char* labels[5] = {"PLAY", "CITY", "OPTIONS", "CONTROLS", "QUIT"};
        const float itemX = 250.0f;
        const float firstY = 214.0f;
        const float spacing = 44.0f;

        // Subtle dark strip keeps the gold text readable over the rusty art.
        drawRect(renderer, frame.offsetX + 138.0f * frame.scale,
                 frame.offsetY + 194.0f * frame.scale,
                 386.0f * frame.scale, 236.0f * frame.scale, 0.0f, 0.0f, 0.0f,
                 0.22f);

        for (int i = 0; i < 5; ++i) {
            const float y = firstY + i * spacing;
            const bool selected = menuIndex_ == i;
            if (selected) {
                drawRect(renderer, frame.offsetX + 150.0f * frame.scale,
                         frame.offsetY + (y - 8.0f) * frame.scale,
                         360.0f * frame.scale, 42.0f * frame.scale, 0.0f, 0.0f,
                         0.0f, 0.55f);
                drawRect(renderer, frame.offsetX + 150.0f * frame.scale,
                         frame.offsetY + (y - 8.0f) * frame.scale,
                         3.0f * frame.scale, 42.0f * frame.scale, 1.0f, 0.82f,
                         0.22f, 1.0f);
                const float labelWidth =
                    menuFont_.valid()
                        ? menuFont_.measure(labels[i], 0.85f * frame.scale)
                        : 200.0f * frame.scale;
                drawRect(renderer, frame.offsetX + itemX * frame.scale,
                         frame.offsetY + (y + 30.0f) * frame.scale,
                         labelWidth, 3.0f * frame.scale, 1.0f, 0.82f, 0.22f,
                         0.95f);
                if (keyFont_.valid()) {
                    const int keyFrame =
                        static_cast<int>(menuTime_ * 8.0f) % 13;
                    keyFont_.drawGlyph(
                        renderer.spriteShader(), keyFrame,
                        frame.offsetX + 158.0f * frame.scale,
                        frame.offsetY + (y - 6.0f) * frame.scale,
                        0.62f * frame.scale, 1.0f, 1.0f, 1.0f, 1.0f,
                        renderer.width(), renderer.height());
                }
            }
            drawOriginalText(renderer, frame, labels[i], itemX, y, 0.85f,
                             selected ? 1.0f : 0.66f,
                             selected ? 1.0f : 0.66f,
                             selected ? 0.72f : 0.45f, 1.0f, false);
            if (i == 1) {
                const std::string city = cityDisplayName(selectedCity_);
                const std::string value =
                    selected ? "< " + city + " >" : city;
                drawOriginalText(renderer, frame, value, 412.0f, y + 8.0f,
                                 0.55f, 0.95f, 0.95f, 0.88f, 0.95f, false);
            }
        }

        if (statusTimer_ > 0.0f && !statusMessage_.empty()) {
            drawOriginalText(renderer, frame, statusMessage_, 320.0f, 432.0f,
                             0.5f, 1.0f, 0.7f, 0.28f, 0.95f, true);
        }
        drawOriginalText(renderer, frame,
                         "UP / DOWN  SELECT      ENTER / A  CONFIRM      LEFT / RIGHT  CHANGE",
                         320.0f, 452.0f, 0.42f, 0.78f, 0.82f, 0.88f, 0.92f,
                         true);
        drawOriginalText(renderer, frame,
                         "F1 FULLSCREEN    F2 FXAA    F3 DEBUG    F4 MINIMAP",
                         320.0f, 466.0f, 0.38f, 0.6f, 0.66f, 0.74f, 0.85f,
                         true);
        drawText(renderer, "v0.2.0", w - 18.0f * s, h - 20.0f * s, 0.75f * s,
                 0.45f, 0.5f, 0.58f, 0.7f, false);
        return;
    }

    drawRect(renderer, 0, 0, w, h, 0, 0, 0, 0.56f);
    drawText(renderer, "GRAND THEFT AUTO", w * 0.5f, h * 0.075f, 6.0f * s,
             1.0f, 0.83f, 0.25f, 1.0f, true);
    drawRect(renderer, w * 0.5f - 250.0f * s, h * 0.30f, 500.0f * s,
             2.0f * s, 0.85f, 0.2f, 0.15f, 0.9f);
    drawText(renderer, "DOS RECOMPILATION", w * 0.5f, h * 0.19f, 3.0f * s,
             0.82f, 0.88f, 1.0f, 0.95f, true);

    const float width = menuItemWidth();
    const float panelX = (w - width) * 0.5f;
    const float panelY = menuFirstY(5, 0.55f) - 22.0f * s;
    drawRect(renderer, panelX - 16.0f * s, panelY, width + 32.0f * s,
             menuItemSpacing() * 4.6f + 20.0f * s, 0.0f, 0.0f, 0.0f, 0.45f);

    const char* labels[5] = {"PLAY", "CITY", "OPTIONS", "CONTROLS", "QUIT"};
    for (int i = 0; i < 5; ++i) {
        std::string value;
        if (i == 1) value = cityDisplayName(selectedCity_);
        if (i == 1) {
            drawMenuItem(renderer, i, 5, 0.55f, labels[i],
                         "< " + value + " >", menuIndex_ == i, true);
        } else {
            drawMenuItem(renderer, i, 5, 0.55f, labels[i], value,
                         menuIndex_ == i, false);
        }
    }

    drawText(renderer, "UP / DOWN  SELECT      ENTER / A  CONFIRM      LEFT / RIGHT  CHANGE",
             w * 0.5f, h - 50.0f * s, 1.0f * s, 0.72f, 0.78f, 0.88f, 0.9f, true);
    drawText(renderer, "F1 FULLSCREEN   F2 FXAA   F3 DEBUG   F4 MINIMAP", w * 0.5f,
             h - 30.0f * s, 0.9f * s, 0.58f, 0.63f, 0.72f, 0.85f, true);
    if (statusTimer_ > 0.0f && !statusMessage_.empty()) {
        drawText(renderer, statusMessage_, w * 0.5f, h - 76.0f * s, 1.0f * s,
                 1.0f, 0.7f, 0.25f, 0.95f, true);
    }
}

void Game::renderSettings(Renderer& renderer, bool fromPause) {
    const float w = static_cast<float>(renderer.width());
    const float h = static_cast<float>(renderer.height());
    const float s = uiScale();
    (void)w;
    (void)h;
    if (originalAssets_ && !fromPause) {
        drawOriginalBackdrop(renderer, false);
    } else {
        drawRect(renderer, 0, 0, w, h, 0, 0, 0, 0.72f);
    }
    const MenuFrame frame = menuFrame();
    const float fs = frame.scale;
    drawRect(renderer, frame.offsetX + 112.0f * fs, frame.offsetY + 78.0f * fs,
             416.0f * fs, 356.0f * fs, 0.0f, 0.0f, 0.0f, 0.58f);
    drawRect(renderer, frame.offsetX + 112.0f * fs, frame.offsetY + 78.0f * fs,
             416.0f * fs, 2.0f * fs, 0.85f, 0.7f, 0.25f, 0.95f);

    const std::string title = fromPause ? "PAUSED - OPTIONS" : "OPTIONS";
    drawOriginalText(renderer, frame, title, 320.0f, 88.0f, 0.85f, 1.0f, 0.88f,
                     0.4f, 1.0f, true);

    char resolution[32];
    std::snprintf(resolution, sizeof(resolution), "%d X %d",
                  config_.graphics.width, config_.graphics.height);
    const std::string values[8] = {
        resolution,
        config_.graphics.fullscreen ? "ON" : "OFF",
        std::to_string(config_.graphics.msaa),
        config_.graphics.fxaa ? "ON" : "OFF",
        config_.audio.music ? "ON" : "OFF",
        percentText(config_.audio.musicVolume),
        percentText(config_.audio.engineVolume),
        "",
    };
    const char* labels[8] = {"RESOLUTION", "FULLSCREEN", "MSAA",       "FXAA",
                             "MUSIC",      "MUSIC VOLUME", "ENGINE VOLUME",
                             "BACK"};
    const float firstY = 150.0f;
    const float spacing = 38.0f;
    for (int i = 0; i < 8; ++i) {
        const float y = firstY + i * spacing;
        const bool selected = settingsIndex_ == i;
        if (selected) {
            drawRect(renderer, frame.offsetX + 140.0f * fs,
                     frame.offsetY + (y - 7.0f) * fs, 360.0f * fs,
                     32.0f * fs, 0.05f, 0.05f, 0.03f, 0.72f);
            drawRect(renderer, frame.offsetX + 140.0f * fs,
                     frame.offsetY + (y - 7.0f) * fs, 3.0f * fs, 32.0f * fs,
                     1.0f, 0.82f, 0.22f, 1.0f);
        }
        drawOriginalText(renderer, frame, labels[i], 166.0f, y, 0.55f,
                         selected ? 1.0f : 0.78f, selected ? 0.95f : 0.78f,
                         selected ? 0.55f : 0.6f, 1.0f, false);
        if (i < 7) {
            const std::string shown = "< " + values[i] + " >";
            const float valueScale = 0.46f * fs;
            const float valueWidth =
                missionFont_.valid() ? missionFont_.measure(shown, valueScale)
                                     : 100.0f;
            drawHudText(renderer, missionFont_, shown,
                        frame.offsetX + 492.0f * fs - valueWidth,
                        frame.offsetY + y * fs, valueScale,
                        selected ? 1.0f : 0.72f, selected ? 0.92f : 0.76f,
                        selected ? 0.55f : 0.66f, 0.95f, false, true);
        } else {
            drawOriginalText(renderer, frame, "ENTER", 430.0f, y, 0.46f,
                             0.72f, 0.76f, 0.82f, 0.9f, false);
        }
    }
    drawOriginalText(renderer, frame,
                     "LEFT / RIGHT  CHANGE      ENTER  CONFIRM      ESC  BACK",
                     320.0f, 458.0f, 0.42f, 0.75f, 0.8f, 0.86f, 0.92f, true);
}

void Game::renderControls(Renderer& renderer) {
    const float w = static_cast<float>(renderer.width());
    const float h = static_cast<float>(renderer.height());
    if (originalAssets_) {
        drawOriginalBackdrop(renderer, false);
        drawRect(renderer, 0, 0, w, h, 0, 0, 0, 0.35f);
    } else {
        drawRect(renderer, 0, 0, w, h, 0, 0, 0, 0.72f);
    }
    const MenuFrame frame = menuFrame();
    const float fs = frame.scale;
    drawRect(renderer, frame.offsetX + 54.0f * fs, frame.offsetY + 60.0f * fs,
             532.0f * fs, 372.0f * fs, 0.0f, 0.0f, 0.0f, 0.60f);
    drawOriginalText(renderer, frame, "CONTROLS", 320.0f, 74.0f, 0.9f, 1.0f,
                     0.88f, 0.4f, 1.0f, true);

    const char* leftKeys[] = {"WASD", "SPACE", "F / E", "TAB", "P / ESC", "R"};
    const char* leftActions[] = {"DRIVE / WALK", "HANDBRAKE", "ENTER CAR",
                                 "CITY MAP",     "PAUSE",     "RESET CAR"};
    const char* rightKeys[] = {"+ / -", "C", "Q", "M", "F1", "F3", "F12"};
    const char* rightActions[] = {"ZOOM", "CAMERA", "WEAPON", "RADIO",
                                  "FULLSCREEN", "DEBUG", "SCREENSHOT"};
    float y = 132.0f;
    for (int i = 0; i < 6; ++i) {
        drawOriginalText(renderer, frame, leftKeys[i], 60.0f, y, 0.4f, 1.0f,
                         0.86f, 0.4f, 1.0f, false);
        drawOriginalText(renderer, frame, leftActions[i], 176.0f, y, 0.4f,
                         0.86f, 0.9f, 0.96f, 0.95f, false);
        y += 32.0f;
    }
    y = 132.0f;
    for (int i = 0; i < 7; ++i) {
        drawOriginalText(renderer, frame, rightKeys[i], 330.0f, y, 0.4f, 1.0f,
                         0.86f, 0.4f, 1.0f, false);
        drawOriginalText(renderer, frame, rightActions[i], 408.0f, y, 0.4f,
                         0.86f, 0.9f, 0.96f, 0.95f, false);
        y += 32.0f;
    }
    drawOriginalText(renderer, frame, "ESC / ENTER  BACK", 320.0f, 452.0f,
                     0.46f, 0.75f, 0.8f, 0.86f, 0.92f, true);
}

void Game::renderCityMap(Renderer& renderer) {
    const float w = static_cast<float>(renderer.width());
    const float h = static_cast<float>(renderer.height());
    const float s = uiScale();
    drawRect(renderer, 0, 0, w, h, 0.025f, 0.035f, 0.045f, 1.0f);
    if (!assets_.minimapTexture()) {
        drawHudText(renderer, missionHeaderFont_, "NO MAP DATA", w * 0.5f,
                    h * 0.5f, 2.0f * s, 1.0f, 0.8f, 0.3f, 1.0f, true, true);
        return;
    }

    const float size = std::min(w, h) * 0.74f;
    const float x = (w - size) * 0.5f;
    const float y = (h - size) * 0.5f + 28.0f * s;

    // Gold frame and subtle drop shadow.
    drawRect(renderer, x - 8.0f * s, y - 8.0f * s, size + 16.0f * s,
             size + 16.0f * s, 0.72f, 0.62f, 0.28f, 1.0f);
    drawRect(renderer, x - 4.0f * s, y - 4.0f * s, size + 8.0f * s,
             size + 8.0f * s, 0.10f, 0.11f, 0.13f, 1.0f);
    drawTexturedRect(renderer, assets_.minimapTexture(), x, y, size, size, 0, 0,
                     1, 1, 1.0f, 1.0f, 1.0f, 1.0f);

    auto toScreen = [&](const Vec3& p, float& ox, float& oy) {
        ox = x + std::clamp(p.x / 256.0f, 0.0f, 1.0f) * size;
        oy = y + std::clamp(p.z / 256.0f, 0.0f, 1.0f) * size;
    };

    // Payphones: the next one is highlighted, the rest are dim.
    for (size_t i = 0; i < phones_.size(); ++i) {
        if (!phones_[i].active) continue;
        float mx = 0.0f, my = 0.0f;
        toScreen(phones_[i].pos, mx, my);
        const bool next = missionState_ == MissionState::Idle &&
                          static_cast<int>(i) == activePhone_;
        const float r = (next ? 4.0f : 2.2f) * s;
        drawRect(renderer, mx - r - 1.0f * s, my - r - 1.0f * s,
                 r * 2.0f + 2.0f * s, r * 2.0f + 2.0f * s, 0.0f, 0.0f, 0.0f,
                 0.85f);
        drawRect(renderer, mx - r, my - r, r * 2.0f, r * 2.0f,
                 next ? 0.2f : 0.35f, next ? 1.0f : 0.75f, 0.5f, 1.0f);
    }

    // Active mission destination.
    if (missionState_ == MissionState::Active) {
        float mx = 0.0f, my = 0.0f;
        toScreen(targetPos_, mx, my);
        const float pulse = (5.0f + std::sin(worldTime_ * 5.0f) * 1.5f) * s;
        drawRect(renderer, mx - pulse - 1.5f * s, my - pulse - 1.5f * s,
                 pulse * 2.0f + 3.0f * s, pulse * 2.0f + 3.0f * s, 0.0f, 0.0f,
                 0.0f, 0.9f);
        drawRect(renderer, mx - pulse, my - pulse, pulse * 2.0f, pulse * 2.0f,
                 1.0f, 0.8f, 0.2f, 1.0f);
    }

    // Player: white arrow showing the facing direction.
    {
        float mx = 0.0f, my = 0.0f;
        toScreen(playerPosition(), mx, my);
        const float yaw = inCar_ ? vehicle_.yaw() : footYaw_;
        const float fx = std::sin(yaw) * 9.0f * s;
        const float fz = -std::cos(yaw) * 9.0f * s;
        const float rx = std::cos(yaw) * 5.0f * s;
        const float rz = std::sin(yaw) * 5.0f * s;
        drawColoredTriangle(renderer, Vec3{mx + fx, 0, my + fz},
                            Vec3{mx - rx, 0, my - rz},
                            Vec3{mx + rx, 0, my + rz}, 1.0f, 1.0f, 1.0f, 1.0f);
    }

    const std::string city = assets_.cityName();
    const char* title = "LIBERTY CITY";
    if (city == "SANB") title = "SAN ANDREAS";
    else if (city == "MIAMI") title = "VICE CITY";
    drawHudText(renderer, missionHeaderFont_, title, w * 0.5f,
                y - 52.0f * s, 1.7f * s, 1.0f, 0.88f, 0.35f, 1.0f, true,
                true);

    // Legend.
    const float legendY = y + size + 18.0f * s;
    const float legendX = x + 8.0f * s;
    auto legend = [&](float lx, float r, float g, float b, const char* label) {
        drawRect(renderer, lx, legendY, 12.0f * s, 12.0f * s, r, g, b, 1.0f);
        drawHudText(renderer, missionFont_, label, lx + 18.0f * s,
                    legendY - 2.0f * s, 0.55f * s, 0.85f, 0.9f, 0.95f, 1.0f,
                    false, false);
    };
    legend(legendX, 0.2f, 1.0f, 0.5f, "PAYPHONE");
    legend(legendX + 150.0f * s, 1.0f, 0.8f, 0.2f, "OBJECTIVE");
    legend(legendX + 320.0f * s, 1.0f, 1.0f, 1.0f, "YOU");

    drawHudText(renderer, missionFont_, "TAB / ESC  CLOSE   F4  MINIMAP",
                w * 0.5f, h - 22.0f * s, 0.55f * s, 0.7f, 0.78f, 0.86f, 0.95f,
                true, false);
}

void Game::renderPause(Renderer& renderer) {
    const float w = static_cast<float>(renderer.width());
    const float h = static_cast<float>(renderer.height());
    drawRect(renderer, 0, 0, w, h, 0, 0, 0, 0.66f);
    const MenuFrame frame = menuFrame();
    const float fs = frame.scale;
    drawRect(renderer, frame.offsetX + 128.0f * fs, frame.offsetY + 150.0f * fs,
             384.0f * fs, 286.0f * fs, 0.0f, 0.0f, 0.0f, 0.64f);
    drawRect(renderer, frame.offsetX + 128.0f * fs, frame.offsetY + 150.0f * fs,
             384.0f * fs, 2.0f * fs, 0.85f, 0.7f, 0.25f, 0.95f);
    drawOriginalText(renderer, frame, "PAUSED", 320.0f, 158.0f, 0.9f, 1.0f,
                     0.86f, 0.35f, 1.0f, true);
    const char* labels[5] = {"RESUME", "RESTART JOB", "OPTIONS", "MAIN MENU",
                             "QUIT"};
    const float firstY = 214.0f;
    for (int i = 0; i < 5; ++i) {
        const float y = firstY + i * 44.0f;
        const bool selected = menuIndex_ == i;
        if (selected) {
            drawRect(renderer, frame.offsetX + 150.0f * fs,
                     frame.offsetY + (y - 8.0f) * fs, 360.0f * fs,
                     40.0f * fs, 0.05f, 0.05f, 0.03f, 0.72f);
            drawRect(renderer, frame.offsetX + 150.0f * fs,
                     frame.offsetY + (y - 8.0f) * fs, 3.0f * fs, 40.0f * fs,
                     1.0f, 0.82f, 0.22f, 1.0f);
        }
        drawOriginalText(renderer, frame, labels[i], 260.0f, y, 0.7f,
                         selected ? 1.0f : 0.7f, selected ? 1.0f : 0.7f,
                         selected ? 0.5f : 0.5f, 1.0f, false);
    }
    drawOriginalText(renderer, frame, "ESC / P  RESUME", 320.0f, 420.0f, 0.42f,
                     0.75f, 0.8f, 0.86f, 0.92f, true);
}

void Game::renderMissionComplete(Renderer& renderer) {
    const float w = static_cast<float>(renderer.width());
    const float h = static_cast<float>(renderer.height());
    const float s = uiScale();
    drawRect(renderer, 0, 0, w, h, 0, 0, 0, 0.74f);
    const MenuFrame frame = menuFrame();
    drawOriginalText(renderer, frame, "CITY COMPLETE", 320.0f, 150.0f, 1.05f,
                     1.0f, 0.86f, 0.3f, 1.0f, true);
    drawOriginalText(renderer, frame, "EVERY JOB IN THE CITY IS DONE", 320.0f,
                     200.0f, 0.55f, 0.85f, 0.9f, 0.95f, 1.0f, true);
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "CASH  $%d", score_);
    drawHudText(renderer, missionFont_, buffer, frame.offsetX + 320.0f * frame.scale,
                frame.offsetY + 246.0f * frame.scale, 0.62f * frame.scale,
                1.0f, 0.9f, 0.4f, 1.0f, true, true);
    std::snprintf(buffer, sizeof(buffer), "TIME  %s",
                  formatTime(missionTime_).c_str());
    drawHudText(renderer, missionFont_, buffer, frame.offsetX + 320.0f * frame.scale,
                frame.offsetY + 276.0f * frame.scale, 0.52f * frame.scale,
                0.85f, 0.9f, 0.95f, 1.0f, true, true);
    drawOriginalText(renderer, frame, "ENTER / A  PLAY AGAIN", 320.0f, 348.0f,
                     0.5f, 0.8f, 0.86f, 0.92f, 0.95f, true);
    drawOriginalText(renderer, frame, "ESC / B  MAIN MENU", 320.0f, 372.0f,
                     0.46f, 0.68f, 0.74f, 0.84f, 0.9f, true);
    drawText(renderer, "v0.2.0", w - 18.0f * s, h - 20.0f * s, 0.75f * s,
             0.45f, 0.5f, 0.58f, 0.7f, false);
}

void Game::drawHudText(Renderer& renderer, const BitmapFont& font,
                       const std::string& text, float x, float y, float scale,
                       float r, float g, float b, float a, bool centered,
                       bool outline) {
    if (text.empty()) return;
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    if (font.valid()) {
        float drawX = x;
        if (centered) drawX -= font.measure(text, scale) * 0.5f;
        if (outline) {
            const float o = std::max(1.0f, scale * 1.15f);
            const float offsets[8][2] = {{-o, 0.0f}, {o, 0.0f}, {0.0f, -o},
                                         {0.0f, o},  {-o, -o}, {o, -o},
                                         {-o, o},   {o, o}};
            for (const auto& offset : offsets) {
                font.draw(renderer.spriteShader(), text, drawX + offset[0],
                          y + offset[1], scale, 0.0f, 0.0f, 0.0f, a * 0.85f,
                          renderer.width(), renderer.height());
            }
        }
        font.draw(renderer.spriteShader(), text, drawX, y, scale, r, g, b, a,
                  renderer.width(), renderer.height());
        return;
    }
    drawText(renderer, text, x, y, scale * 6.0f, r, g, b, a, centered);
}

void Game::drawStatusBanner(Renderer& renderer, const std::string& text,
                            float alpha, float r, float g, float b) {
    if (text.empty()) return;
    const float sw = static_cast<float>(renderer.width());
    const float s = uiScale();
    const float scale = 0.55f * s;
    float width = 260.0f * s;
    if (missionFont_.valid()) {
        width = missionFont_.measure(text, scale) + 54.0f * s;
    } else {
        width = textWidth(text, 1.1f * s) + 40.0f * s;
    }
    width = std::min(width, sw - 32.0f * s);
    const float height = 30.0f * s;
    const float x = (sw - width) * 0.5f;
    const float y = 12.0f * s;

    drawRect(renderer, x, y, width, height, 0.015f, 0.06f, 0.02f, 0.80f * alpha);
    drawRect(renderer, x, y, width, 3.0f * s, 0.36f, 0.78f, 0.28f, 0.9f * alpha);
    drawRect(renderer, x, y + height - 3.0f * s, width, 3.0f * s, 0.18f, 0.42f,
             0.14f, 0.9f * alpha);
    drawRect(renderer, x, y, 3.0f * s, height, 0.55f, 0.92f, 0.42f, 0.9f * alpha);
    if (missionFont_.valid()) {
        drawHudText(renderer, missionFont_, text, sw * 0.5f,
                    y + (height - missionFont_.height() * scale) * 0.5f - 2.0f * s,
                    scale, r, g, b, alpha, true, true);
    } else {
        drawText(renderer, text, sw * 0.5f, y + 8.0f * s, 1.1f * s, r, g, b,
                 alpha, true);
    }
}

void Game::drawColoredTriangle(Renderer& renderer, const Vec3& a, const Vec3& b,
                               const Vec3& c, float r, float g, float bl,
                               float alpha) {
    const float sw = static_cast<float>(renderer.width());
    const float sh = static_cast<float>(renderer.height());
    const Mat4 mvp = Mat4::ortho(0.0f, sw, sh, 0.0f, -1.0f, 1.0f);

    CityVertex vertices[3];
    auto put = [&](int i, const Vec3& p) {
        vertices[i] = makeVertex(p, 0.0f, 0.0f, r, g, bl, alpha);
    };
    put(0, a);
    put(1, b);
    put(2, c);

    Shader& shader = renderer.spriteShader();
    shader.use();
    shader.setMat4("uMVP", mvp);
    shader.setInt("uTexture", 0);
    shader.setInt("uAlphaTest", 0);
    shader.setInt("uMask", 0);
    renderer.whiteTexture().bind(0);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBindVertexArray(carVao_);
    glBindBuffer(GL_ARRAY_BUFFER, carVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Game::drawObjectiveArrow(Renderer& renderer, const Vec3& target) {
    const float sw = static_cast<float>(renderer.width());
    const float sh = static_cast<float>(renderer.height());
    const float s = uiScale();
    const Mat4 mvp = camera_.viewProjection();
    const float cx = mvp.m[0] * target.x + mvp.m[4] * target.y +
                     mvp.m[8] * target.z + mvp.m[12];
    const float cy = mvp.m[1] * target.x + mvp.m[5] * target.y +
                     mvp.m[9] * target.z + mvp.m[13];
    const float cw = mvp.m[3] * target.x + mvp.m[7] * target.y +
                     mvp.m[11] * target.z + mvp.m[15];
    const Vec3 player = playerPosition();
    const float dx = target.x - player.x;
    const float dz = target.z - player.z;
    const float distance = std::sqrt(dx * dx + dz * dz);
    if (distance < 4.0f) return;

    const float margin = 54.0f * s;
    const float centerX = sw * 0.5f;
    const float centerY = sh * 0.5f;
    float screenX = centerX;
    float screenY = centerY;
    float dirX = 0.0f;
    float dirY = 0.0f;
    bool onScreen = false;

    if (cw > 0.0001f) {
        const float ndcX = cx / cw;
        const float ndcY = cy / cw;
        screenX = (ndcX * 0.5f + 0.5f) * sw;
        screenY = (1.0f - (ndcY * 0.5f + 0.5f)) * sh;
        onScreen = ndcX > -0.92f && ndcX < 0.92f && ndcY > -0.92f &&
                   ndcY < 0.92f;
        dirX = screenX - centerX;
        dirY = screenY - centerY;
    } else {
        // Target is behind the camera: fall back to the ground-plane
        // direction so the edge arrow still points the right way.
        dirX = dx;
        dirY = dz;
    }
    const float length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length < 1.0f) return;
    dirX /= length;
    dirY /= length;

    float anchorX = screenX;
    float anchorY = screenY;
    if (!onScreen) {
        const float halfW = sw * 0.5f - margin;
        const float halfH = sh * 0.5f - margin;
        const float scaleX =
            std::abs(dirX) > 0.0001f ? halfW / std::abs(dirX) : 1e9f;
        const float scaleY =
            std::abs(dirY) > 0.0001f ? halfH / std::abs(dirY) : 1e9f;
        const float edge = std::min(scaleX, scaleY);
        anchorX = centerX + dirX * edge;
        anchorY = centerY + dirY * edge;
    } else {
        anchorY = std::max(margin, anchorY - 22.0f * s);
    }

    const float perpX = -dirY;
    const float perpY = dirX;
    const float size = 11.0f * s;

    const Vec3 tip{anchorX + dirX * size * 1.4f,
                   anchorY + dirY * size * 1.4f, 0.0f};
    const Vec3 left{anchorX - dirX * size * 0.6f + perpX * size * 0.85f,
                    anchorY - dirY * size * 0.6f + perpY * size * 0.85f, 0.0f};
    const Vec3 right{anchorX - dirX * size * 0.6f - perpX * size * 0.85f,
                     anchorY - dirY * size * 0.6f - perpY * size * 0.85f, 0.0f};
    const Vec3 tipOutline{anchorX + dirX * size * 1.7f,
                          anchorY + dirY * size * 1.7f, 0.0f};
    const Vec3 leftOutline{anchorX - dirX * size * 0.8f + perpX * size * 1.1f,
                           anchorY - dirY * size * 0.8f + perpY * size * 1.1f,
                           0.0f};
    const Vec3 rightOutline{anchorX - dirX * size * 0.8f - perpX * size * 1.1f,
                            anchorY - dirY * size * 0.8f - perpY * size * 1.1f,
                            0.0f};
    drawColoredTriangle(renderer, tipOutline, leftOutline, rightOutline, 0.0f,
                        0.0f, 0.0f, 0.8f);
    drawColoredTriangle(renderer, tip, left, right, 1.0f, 0.84f, 0.22f, 0.95f);

    char label[32];
    std::snprintf(label, sizeof(label), "%d M", static_cast<int>(distance));
    drawHudText(renderer, missionFont_, label, anchorX,
                anchorY + size * 1.8f, 0.4f * s, 1.0f, 0.9f, 0.45f, 0.95f,
                true, true);
}

GLuint Game::createMinimapFrameTexture(int size) {
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4, 0);
    const float half = size * 0.5f;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float dx = (x + 0.5f) - half;
            const float dy = (y + 0.5f) - half;
            const float d = std::sqrt(dx * dx + dy * dy) / half;
            uint8_t* p = &pixels[(static_cast<size_t>(y) * size + x) * 4];
            if (d > 1.02f) continue;
            if (d > 0.965f) {
                p[0] = 4; p[1] = 5; p[2] = 6; p[3] = 235;
            } else if (d > 0.925f) {
                // Gold ring with a slight highlight towards the top-left.
                const float light =
                    std::max(0.0f, (-dx - dy) / (half * 2.0f) + 0.55f);
                p[0] = static_cast<uint8_t>(120 + 95 * light);
                p[1] = static_cast<uint8_t>(98 + 85 * light);
                p[2] = static_cast<uint8_t>(36 + 55 * light);
                p[3] = 255;
            } else if (d > 0.905f) {
                p[0] = 20; p[1] = 18; p[2] = 10; p[3] = 220;
            }
        }
    }
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (!texture) return 0;
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}


void Game::drawHud(Renderer& renderer) {
    const float sw = static_cast<float>(renderer.width());
    const float sh = static_cast<float>(renderer.height());
    const float s = uiScale();
    const float margin = 16.0f * s;

    // Job counter and timer in the top-left corner.
    drawRect(renderer, margin - 7.0f * s, margin - 6.0f * s, 152.0f * s,
             52.0f * s, 0.0f, 0.0f, 0.0f, 0.50f);
    if (!missions_.empty()) {
        char job[48];
        std::snprintf(job, sizeof(job), "JOB %02d/%02d",
                      std::min(missionIndex_ + 1,
                               static_cast<int>(missions_.size())),
                      static_cast<int>(missions_.size()));
        drawHudText(renderer, missionFont_, job, margin, margin, 0.5f * s,
                    1.0f, 0.9f, 0.45f, 1.0f, false, true);
    }
    if (missionState_ == MissionState::Active) {
        drawHudText(renderer, missionFont_, formatTime(missionTimeLeft_),
                    margin, margin + 18.0f * s, 0.5f * s, 1.0f, 0.92f, 0.6f,
                    1.0f, false, true);
        const float barW = 118.0f * s;
        const float barY = margin + 36.0f * s;
        drawRect(renderer, margin, barY, barW, 4.0f * s, 0.0f, 0.0f, 0.0f, 0.7f);
        const float ratio =
            missionTimeLimit_ > 0.0f
                ? std::clamp(missionTimeLeft_ / missionTimeLimit_, 0.0f, 1.0f)
                : 0.0f;
        drawRect(renderer, margin, barY, barW * ratio, 4.0f * s,
                 ratio > 0.35f ? 0.45f : 0.95f,
                 ratio > 0.35f ? 0.9f : 0.25f, 0.25f, 1.0f);
    } else {
        drawHudText(renderer, missionFont_, formatTime(missionTime_), margin,
                    margin + 18.0f * s, 0.44f * s, 0.72f, 0.78f, 0.88f, 0.9f,
                    false, true);
    }

    // Cash in the top-right corner, GTA1 style.
    {
        const std::string cash = "$" + std::to_string(score_);
        const float scale = 0.9f * s;
        const float width = missionFont_.valid()
                                ? missionFont_.measure(cash, scale)
                                : textWidth(cash, 2.2f * s);
        drawHudText(renderer, missionFont_, cash, sw - margin - width,
                    margin - 4.0f * s, scale, 1.0f, 0.9f, 0.35f, 1.0f, false,
                    true);
    }

    // Wanted level stars under the cash counter.
    if (wantedLevel_ > 0) {
        const float starSize = 13.0f * s;
        const float gap = 5.0f * s;
        const float totalW = starSize * 5.0f + gap * 4.0f;
        float sx = sw - margin - totalW;
        const float sy = margin + 28.0f * s;
        for (int i = 0; i < 5; ++i) {
            const bool lit = i < wantedLevel_;
            const bool blink = lit && wantedFlash_ > 0.0f &&
                               std::fmod(worldTime_ * 6.0f, 1.0f) > 0.5f;
            drawRect(renderer, sx - 1.0f * s, sy - 1.0f * s,
                     starSize + 2.0f * s, starSize + 2.0f * s, 0.0f, 0.0f,
                     0.0f, 0.85f);
            drawRect(renderer, sx, sy, starSize, starSize,
                     lit ? 1.0f : 0.14f,
                     lit ? (blink ? 0.45f : 0.82f) : 0.14f,
                     lit ? 0.12f : 0.14f, 1.0f);
            sx += starSize + gap;
        }
    }

    // Player health and armor under the job panel.
    {
        const float barX = margin;
        const float barY = margin + 58.0f * s;
        const float barW = 136.0f * s;
        const float barH = 8.0f * s;
        const float health = std::clamp(playerHealth_ / 100.0f, 0.0f, 1.0f);
        drawRect(renderer, barX, barY, barW, barH, 0.0f, 0.0f, 0.0f, 0.62f);
        drawRect(renderer, barX + 1.0f * s, barY + 1.0f * s,
                 (barW - 2.0f * s) * health, barH - 2.0f * s,
                 health > 0.3f ? 0.85f : 1.0f,
                 health > 0.3f ? 0.25f : 0.1f, 0.2f, 1.0f);
        if (playerArmor_ > 0.0f) {
            const float armor = std::clamp(playerArmor_ / 100.0f, 0.0f, 1.0f);
            drawRect(renderer, barX, barY + barH + 2.0f * s, barW,
                     barH * 0.7f, 0.0f, 0.0f, 0.0f, 0.62f);
            drawRect(renderer, barX + 1.0f * s,
                     barY + barH + 3.0f * s,
                     (barW - 2.0f * s) * armor, barH * 0.7f - 2.0f * s,
                     0.35f, 0.65f, 1.0f, 1.0f);
        }
    }

    // Weapon icon, name and ammunition above the vehicle label.
    if (!inCar_) {
        const int weaponIndex = static_cast<int>(weapon_);
        const float panelW = 176.0f * s;
        const float panelH = 40.0f * s;
        const float px = sw - margin - panelW;
        const float py = sh - 84.0f * s;
        drawRect(renderer, px, py, panelW, panelH, 0.0f, 0.0f, 0.0f, 0.5f);
        const GLuint icon = assets_.spriteTexture(weaponIcon());
        if (icon) {
            drawTexturedRect(renderer, icon, px + 4.0f * s,
                             py + 4.0f * s, 32.0f * s, 32.0f * s, 0, 0, 1, 1,
                             1.0f, 1.0f, 1.0f, 1.0f);
        }
        char ammo[48];
        std::snprintf(ammo, sizeof(ammo), "%s  %d", weaponName().c_str(),
                      ammo_[weaponIndex]);
        drawHudText(renderer, missionFont_, ammo, px + 42.0f * s,
                    py + 12.0f * s, 0.42f * s, 1.0f, 0.94f, 0.7f, 1.0f, false,
                    true);
    }

    // Top-centre status/objective banner.
    std::string banner;
    if (missionState_ == MissionState::Briefing) {
        banner = "JOB BRIEFING";
    } else if (missionState_ == MissionState::Active) {
        banner = objectiveText_.empty() ? "OBJECTIVE" : objectiveText_;
    } else if (missionState_ == MissionState::Result) {
        banner = resultText_;
    } else if (statusTimer_ > 0.0f && !statusMessage_.empty()) {
        banner = statusMessage_;
    } else {
        banner = "FIND A RINGING PAYPHONE";
    }
    drawStatusBanner(renderer, banner, 1.0f, 1.0f, 0.9f, 0.55f);

    // Context-sensitive hint above the bottom edge.
    std::string hint;
    if (missionState_ == MissionState::Idle && phoneHintTimer_ > 0.0f) {
        hint = "ENTER / A   ANSWER THE PAYPHONE";
    } else if (missionState_ == MissionState::Active) {
        const Vec3 player = playerPosition();
        if (missionType_ == MissionType::StealDeliver &&
            missionPhase_ == MissionPhase::GoToTarget && missionCar_.active) {
            const float dx = missionCar_.pos.x - player.x;
            const float dz = missionCar_.pos.z - player.z;
            if (dx * dx + dz * dz < 3.2f * 3.2f)
                hint = "F / E   TAKE THE CAR";
        } else if (missionType_ == MissionType::KillPed) {
            hint = "RUN THE TARGET DOWN";
        } else if (missionType_ == MissionType::DestroyCar) {
            hint = "RAM THE TARGET CAR TO DESTROY IT";
        } else if (missionType_ == MissionType::Pickup &&
                   missionPhase_ == MissionPhase::GoToTarget) {
            hint = "STOP NEXT TO THE CONTACT";
        }
    } else if (!inCar_ && nearVehicle()) {
        hint = "F / E   ENTER THE CAR";
    }
    if (!hint.empty()) {
        const float scale = 0.5f * s;
        float width = missionFont_.valid()
                          ? missionFont_.measure(hint, scale) + 36.0f * s
                          : textWidth(hint, 1.0f * s) + 30.0f * s;
        width = std::min(width, sw - 40.0f * s);
        const float x = (sw - width) * 0.5f;
        const float y = sh - 92.0f * s;
        drawRect(renderer, x, y, width, 26.0f * s, 0.0f, 0.0f, 0.0f, 0.66f);
        drawRect(renderer, x, y, 2.5f * s, 26.0f * s, 1.0f, 0.84f, 0.25f, 0.95f);
        drawHudText(renderer, missionFont_, hint, sw * 0.5f,
                    y + 4.0f * s, scale, 1.0f, 0.94f, 0.66f, 1.0f, true, true);
    }

    // Carrier / vehicle state in the bottom-right corner.
    if (inCar_) {
        char speed[96];
        std::snprintf(speed, sizeof(speed), "%s   %d MPH", carLabel_.c_str(),
                      static_cast<int>(std::abs(vehicle_.speed()) * 6.0f));
        const float scale = 0.42f * s;
        const float width = missionFont_.valid()
                                ? missionFont_.measure(speed, scale)
                                : textWidth(speed, 1.0f * s);
        drawHudText(renderer, missionFont_, speed, sw - margin - width,
                    sh - 34.0f * s, scale, 0.9f, 0.94f, 1.0f, 0.95f, false,
                    true);
    } else {
        drawHudText(renderer, missionFont_, "ON FOOT", sw - margin - 54.0f * s,
                    sh - 34.0f * s, 0.42f * s, 0.75f, 0.82f, 0.9f, 0.9f, false,
                    true);
    }

    // Minimap in the bottom-left corner.
    drawMinimap(renderer);

    // Yellow waypoint arrow at the screen edge.
    if (missionState_ == MissionState::Active) {
        drawObjectiveArrow(renderer, targetPos_);
    } else if (missionState_ == MissionState::Idle && !phones_.empty()) {
        drawObjectiveArrow(renderer, phones_[static_cast<size_t>(activePhone_)].pos);
    }

    if (missionFlash_ > 0.0f) {
        const bool good = resultText_.find("COMPLETE") != std::string::npos;
        drawRect(renderer, 0, 0, sw, sh, good ? 0.2f : 1.0f,
                 good ? 1.0f : 0.2f, good ? 0.35f : 0.15f,
                 missionFlash_ * 0.28f);
    }

    if (hurtFlash_ > 0.0f) {
        drawRect(renderer, 0, 0, sw, sh, 0.85f, 0.03f, 0.03f,
                 hurtFlash_ * 0.24f);
    }
    if (deathFade_ > 0.001f) {
        drawRect(renderer, 0, 0, sw, sh, 0.28f, 0.0f, 0.0f,
                 deathFade_ * 0.55f);
    }
    if (wastedTimer_ > 0.0f || bustedTimer_ > 0.0f) {
        const bool wasted = wastedTimer_ > 0.0f;
        const float scale = 2.6f * s;
        const char* text = wasted ? "WASTED" : "BUSTED";
        drawHudText(renderer, missionHeaderFont_, text, sw * 0.5f,
                    sh * 0.40f, scale, wasted ? 0.95f : 0.45f,
                    wasted ? 0.12f : 0.6f, wasted ? 0.12f : 1.0f, 1.0f, true,
                    true);
    }

    if (showDebug_) {
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer),
                      "POS %.1f %.1f  YAW %.0f  FPS %.0f  ZOOM %.2f",
                      playerPosition().x, playerPosition().z,
                      vehicle_.yaw() * 180.0f / kPi, fps_, camera_.zoom());
        drawHudText(renderer, missionFont_, buffer, margin,
                    sh - 58.0f * s, 0.38f * s, 0.6f, 1.0f, 0.7f, 0.95f, false,
                    true);
        std::snprintf(buffer, sizeof(buffer), "TRAFFIC %zu  PEDS %zu  SPRITE %d",
                      traffic_.size(), pedestrians_.size(), spriteIndex_);
        drawHudText(renderer, missionFont_, buffer, margin,
                    sh - 44.0f * s, 0.38f * s, 0.6f, 1.0f, 0.7f, 0.95f, false,
                    true);
    }
}

void Game::drawMinimap(Renderer& renderer) {
    if (!minimapEnabled_ || !assets_.minimapTexture()) return;
    const float sw = static_cast<float>(renderer.width());
    const float sh = static_cast<float>(renderer.height());
    const float s = uiScale();
    (void)sw;
    const float size = 176.0f * s;
    const float margin = 16.0f * s;
    const float x = margin;
    const float y = sh - margin - size;

    // Soft drop shadow behind the circular frame.
    if (minimapFrame_) {
        drawTexturedRect(renderer, minimapFrame_, x - 5.0f * s, y - 5.0f * s,
                         size + 10.0f * s, size + 10.0f * s, 0, 0, 1, 1, 0.0f,
                         0.0f, 0.0f, 0.55f);
    }

    const float inset = 5.0f * s;
    const float mapSize = size - inset * 2.0f;
    const float mapX = x + inset;
    const float mapY = y + inset;
    const Vec3 player = playerPosition();
    const float span = 58.0f;
    float u0 = (player.x - span * 0.5f) / 256.0f;
    float v0 = (player.z - span * 0.5f) / 256.0f;
    const float uvSpan = span / 256.0f;
    u0 = std::clamp(u0, 0.0f, 1.0f - uvSpan);
    v0 = std::clamp(v0, 0.0f, 1.0f - uvSpan);
    const float u1 = u0 + uvSpan;
    const float v1 = v0 + uvSpan;

    drawTexturedRectMasked(renderer, assets_.minimapTexture(), mapX, mapY,
                           mapSize, mapSize, u0, v0, u1, v1, 1.0f, 1.0f, 1.0f,
                           1.0f, (u0 + u1) * 0.5f, (v0 + v1) * 0.5f,
                           uvSpan * 0.5f);
    if (minimapFrame_) {
        drawTexturedRect(renderer, minimapFrame_, x, y, size, size, 0, 0, 1, 1,
                         1.0f, 1.0f, 1.0f, 1.0f);
    }

    const float cx = x + size * 0.5f;
    const float cy = y + size * 0.5f;
    const float radius = size * 0.5f - 6.0f * s;

    auto mapOffset = [&](const Vec3& p, float& outX, float& outY) {
        outX = (p.x - player.x) / span * mapSize;
        outY = (p.z - player.z) / span * mapSize;
        const float length = std::sqrt(outX * outX + outY * outY);
        const float limit = radius - 5.0f * s;
        if (length > limit && length > 0.001f) {
            outX *= limit / length;
            outY *= limit / length;
        }
        outX += cx;
        outY += cy;
    };

    // Mission objective marker.
    if (missionState_ == MissionState::Active) {
        float mx = 0.0f, my = 0.0f;
        mapOffset(targetPos_, mx, my);
        const float pulse = 3.5f * s + std::sin(worldTime_ * 5.0f) * 1.2f * s;
        const bool hostile = missionType_ == MissionType::KillPed ||
                             missionType_ == MissionType::DestroyCar;
        drawRect(renderer, mx - pulse - 2.0f * s, my - pulse - 2.0f * s,
                 pulse * 2.0f + 4.0f * s, pulse * 2.0f + 4.0f * s, 0.0f, 0.0f,
                 0.0f, 0.9f);
        drawRect(renderer, mx - pulse, my - pulse, pulse * 2.0f, pulse * 2.0f,
                 hostile ? 1.0f : 1.0f, hostile ? 0.25f : 0.82f,
                 hostile ? 0.2f : 0.2f, 1.0f);
    } else if (missionState_ == MissionState::Idle && !phones_.empty()) {
        float mx = 0.0f, my = 0.0f;
        mapOffset(phones_[static_cast<size_t>(activePhone_)].pos, mx, my);
        const float pulse = 3.0f * s + std::sin(worldTime_ * 4.0f) * 0.8f * s;
        drawRect(renderer, mx - pulse - 2.0f * s, my - pulse - 2.0f * s,
                 pulse * 2.0f + 4.0f * s, pulse * 2.0f + 4.0f * s, 0.0f, 0.0f,
                 0.0f, 0.9f);
        drawRect(renderer, mx - pulse, my - pulse, pulse * 2.0f, pulse * 2.0f,
                 0.2f, 1.0f, 0.85f, 1.0f);
    }

    // Dynamic blips: pickups, traffic, police and officers.
    for (const WeaponPickup& pickup : weaponPickups_) {
        if (!pickup.active) continue;
        float r = 1.0f, g = 0.85f, b = 0.25f;
        if (pickup.kind == PickupKind::Health) {
            r = 0.35f;
            g = 1.0f;
            b = 0.4f;
        } else if (pickup.kind == PickupKind::Armor) {
            r = 0.4f;
            g = 0.7f;
            b = 1.0f;
        }
        const float dx = (pickup.pos.x - player.x) / span * mapSize;
        const float dy = (pickup.pos.z - player.z) / span * mapSize;
        if (std::sqrt(dx * dx + dy * dy) > mapSize * 0.5f - 4.0f * s) continue;
        const float radiusPx = 2.2f * s;
        drawRect(renderer, cx + dx - radiusPx, cy + dy - radiusPx,
                 radiusPx * 2.0f, radiusPx * 2.0f, r, g, b, 1.0f);
    }
    for (const TrafficCar& car : traffic_) {
        if (car.dead) continue;
        const float dx = (car.pos.x - player.x) / span * mapSize;
        const float dy = (car.pos.z - player.z) / span * mapSize;
        if (std::sqrt(dx * dx + dy * dy) > mapSize * 0.5f - 4.0f * s) continue;
        drawRect(renderer, cx + dx - 1.6f * s, cy + dy - 1.6f * s,
                 3.2f * s, 3.2f * s, 0.62f, 0.66f, 0.72f, 0.85f);
    }
    for (const PoliceCar& car : policeCars_) {
        if (!car.active) continue;
        const float flash = std::sin(worldTime_ * 12.0f);
        float bx = 0.0f, by = 0.0f;
        mapOffset(car.pos, bx, by);
        drawRect(renderer, bx - 3.2f * s, by - 3.2f * s, 6.4f * s, 6.4f * s,
                 flash > 0.0f ? 1.0f : 0.15f, 0.1f,
                 flash > 0.0f ? 0.15f : 1.0f, 1.0f);
    }
    for (const Cop& cop : cops_) {
        if (!cop.active) continue;
        float bx = 0.0f, by = 0.0f;
        mapOffset(cop.pos, bx, by);
        drawRect(renderer, bx - 2.4f * s, by - 2.4f * s, 4.8f * s, 4.8f * s,
                 cop.dead ? 0.45f : 0.25f, cop.dead ? 0.25f : 0.45f, 1.0f, 1.0f);
    }

    // Player arrow, north-up map.
    const float yaw = inCar_ ? vehicle_.yaw() : footYaw_;
    const float fx = std::sin(yaw);
    const float fz = -std::cos(yaw);
    const float px = -fz;
    const float pz = fx;
    const float arrow = 9.0f * s;
    const Vec3 tip{cx + fx * arrow, cy + fz * arrow, 0.0f};
    const Vec3 left{cx - fx * arrow * 0.55f + px * arrow * 0.65f,
                    cy - fz * arrow * 0.55f + pz * arrow * 0.65f, 0.0f};
    const Vec3 right{cx - fx * arrow * 0.55f - px * arrow * 0.65f,
                     cy - fz * arrow * 0.55f - pz * arrow * 0.65f, 0.0f};
    const Vec3 tipO{cx + fx * arrow * 1.25f, cy + fz * arrow * 1.25f, 0.0f};
    const Vec3 leftO{cx - fx * arrow * 0.7f + px * arrow * 0.85f,
                     cy - fz * arrow * 0.7f + pz * arrow * 0.85f, 0.0f};
    const Vec3 rightO{cx - fx * arrow * 0.7f - px * arrow * 0.85f,
                      cy - fz * arrow * 0.7f - pz * arrow * 0.85f, 0.0f};
    drawColoredTriangle(renderer, tipO, leftO, rightO, 0.0f, 0.0f, 0.0f, 0.85f);
    drawColoredTriangle(renderer, tip, left, right, 1.0f, 1.0f, 1.0f, 1.0f);

    drawHudText(renderer, missionFont_, "N", cx, y + 6.0f * s, 0.42f * s,
                0.95f, 0.92f, 0.72f, 0.95f, true, true);
}

void Game::drawRect(Renderer& renderer, float x, float y, float w, float h,
                    float r, float g, float b, float a) {
    const float sw = static_cast<float>(renderer.width());
    const float sh = static_cast<float>(renderer.height());
    const Mat4 mvp = Mat4::ortho(0.0f, sw, sh, 0.0f, -1.0f, 1.0f);

    CityVertex v[6];
    auto put = [&](int i, float px, float py, float u, float vv) {
        v[i] = makeVertex(Vec3{px, py, 0.0f}, u, vv, r, g, b, a);
    };
    put(0, x, y, 0, 0);
    put(1, x + w, y, 1, 0);
    put(2, x + w, y + h, 1, 1);
    put(3, x, y, 0, 0);
    put(4, x + w, y + h, 1, 1);
    put(5, x, y + h, 0, 1);

    Shader& shader = renderer.spriteShader();
    shader.use();
    shader.setMat4("uMVP", mvp);
    shader.setInt("uTexture", 0);
    shader.setInt("uAlphaTest", 0);
    renderer.whiteTexture().bind(0);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);

    glBindVertexArray(quadVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Game::drawTexturedRect(Renderer& renderer, GLuint texture, float x, float y,
                            float w, float h, float u0, float v0, float u1,
                            float v1, float r, float g, float b, float a) {
    if (!texture) return;
    const float sw = static_cast<float>(renderer.width());
    const float sh = static_cast<float>(renderer.height());
    const Mat4 mvp = Mat4::ortho(0.0f, sw, sh, 0.0f, -1.0f, 1.0f);
    CityVertex v[6];
    auto put = [&](int i, float px, float py, float u, float vv) {
        v[i] = makeVertex(Vec3{px, py, 0.0f}, u, vv, r, g, b, a);
    };
    put(0, x, y, u0, v0);
    put(1, x + w, y, u1, v0);
    put(2, x + w, y + h, u1, v1);
    put(3, x, y, u0, v0);
    put(4, x + w, y + h, u1, v1);
    put(5, x, y + h, u0, v1);

    Shader& shader = renderer.spriteShader();
    shader.use();
    shader.setMat4("uMVP", mvp);
    shader.setInt("uTexture", 0);
    shader.setInt("uAlphaTest", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);

    glBindVertexArray(quadVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Game::drawTexturedRectMasked(Renderer& renderer, GLuint texture, float x,
                               float y, float w, float h, float u0, float v0,
                               float u1, float v1, float r, float g, float b,
                               float a, float maskCenterU, float maskCenterV,
                               float maskRadius) {
    if (!texture) return;
    const float sw = static_cast<float>(renderer.width());
    const float sh = static_cast<float>(renderer.height());
    const Mat4 mvp = Mat4::ortho(0.0f, sw, sh, 0.0f, -1.0f, 1.0f);
    CityVertex v[6];
    auto put = [&](int i, float px, float py, float u, float vv) {
        v[i] = makeVertex(Vec3{px, py, 0.0f}, u, vv, r, g, b, a);
    };
    put(0, x, y, u0, v0);
    put(1, x + w, y, u1, v0);
    put(2, x + w, y + h, u1, v1);
    put(3, x, y, u0, v0);
    put(4, x + w, y + h, u1, v1);
    put(5, x, y + h, u0, v1);

    Shader& shader = renderer.spriteShader();
    shader.use();
    shader.setMat4("uMVP", mvp);
    shader.setInt("uTexture", 0);
    shader.setInt("uAlphaTest", 0);
    shader.setInt("uMask", 1);
    shader.setVec2("uMaskCenter", maskCenterU, maskCenterV);
    shader.setFloat("uMaskRadius", maskRadius);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);

    glBindVertexArray(quadVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    shader.setInt("uMask", 0);
}

void Game::drawText(Renderer& renderer, const std::string& text, float x,
                    float y, float scale, float r, float g, float b, float a,
                    bool centered, bool shadow) {
    const float sw = static_cast<float>(renderer.width());
    const float sh = static_cast<float>(renderer.height());
    if (centered) x -= textWidth(text, scale) * 0.5f;
    if (shadow) {
        const float offset = std::max(1.0f, scale * 0.4f);
        font_.draw(renderer.spriteShader(), text, x + offset, y + offset, scale,
                   0.0f, 0.0f, 0.0f, a * 0.65f, static_cast<int>(sw),
                   static_cast<int>(sh));
    }
    font_.draw(renderer.spriteShader(), text, x, y, scale, r, g, b, a,
               static_cast<int>(sw), static_cast<int>(sh));
}

void Game::drawMenuItem(Renderer& renderer, int index, int count,
                        float topFraction, const std::string& label,
                        const std::string& value, bool selected,
                        bool showArrows) {
    const float w = static_cast<float>(renderer.width());
    const float s = uiScale();
    const float width = menuItemWidth();
    const float x = (w - width) * 0.5f;
    const float y = menuFirstY(count, topFraction) + index * menuItemSpacing();
    if (selected) {
        drawRect(renderer, x - 10.0f * s, y - 6.0f * s, width + 20.0f * s,
                 menuItemSpacing() * 0.82f, 0.95f, 0.75f, 0.2f, 0.24f);
        drawRect(renderer, x - 10.0f * s, y - 6.0f * s, 4.0f * s,
                 menuItemSpacing() * 0.82f, 1.0f, 0.83f, 0.25f, 1.0f);
    }

    const float textScale = 3.0f * s;
    const float textY = y;
    if (originalAssets_ && menuFont_.valid()) {
        menuFont_.draw(renderer.spriteShader(), label, x + 8.0f * s, textY,
                       textScale, selected ? 1.0f : 0.72f,
                       selected ? 1.0f : 0.72f,
                       selected ? 1.0f : 0.55f, 1.0f, renderer.width(),
                       renderer.height());
    } else {
        drawText(renderer, label, x + 8.0f * s, textY, 1.7f * s,
                 selected ? 1.0f : 0.85f,
                 selected ? 0.86f : 0.89f,
                 selected ? 0.35f : 1.0f, 1.0f);
    }
    if (!value.empty()) {
        const float valueScale = 1.25f * s;
        const float valueWidth = textWidth(value, valueScale);
        drawText(renderer, value, x + width - 8.0f * s - valueWidth, textY + 4.0f * s,
                 valueScale, selected ? 1.0f : 0.72f,
                 selected ? 0.92f : 0.78f,
                 selected ? 0.6f : 0.86f, 0.95f);
    }
    (void)showArrows;
}

float Game::textWidth(const std::string& text, float scale) const {
    return font_.measure(text, scale);
}

} // namespace gta
