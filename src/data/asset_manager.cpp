#include "data/asset_manager.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <unordered_map>

#include <SDL.h>

#include "graphics/texture.h"

namespace gta {
namespace fs = std::filesystem;

namespace {

bool looksLikeDataDir(const fs::path& p) {
    std::error_code ec;
    if (!fs::is_directory(p, ec)) return false;
    return fs::exists(p / "NYC.CMP", ec) || fs::exists(p / "MISSION.INI", ec);
}

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

} // namespace

std::string AssetManager::findDataDir(const std::string& explicitPath) {
    std::vector<fs::path> candidates;
    if (!explicitPath.empty()) candidates.emplace_back(explicitPath);
    if (const char* env = std::getenv("GTA_DATA_DIR")) candidates.emplace_back(env);
#ifdef GTA_DEFAULT_DATA_DIR
    candidates.emplace_back(GTA_DEFAULT_DATA_DIR);
#endif
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (!ec) {
        candidates.push_back(cwd / "game_data" / "GTADATA");
        candidates.push_back(cwd / "GTADATA");
        candidates.push_back(cwd / "game_data");
    }
    if (const char* base = SDL_GetBasePath()) {
        fs::path bp(base);
        candidates.push_back(bp / "GTADATA");
        candidates.push_back(bp / ".." / "game_data" / "GTADATA");
        candidates.push_back(bp / ".." / "Resources" / "GTADATA");
    }

    for (const fs::path& candidate : candidates) {
        if (looksLikeDataDir(candidate)) {
            std::error_code ec2;
            fs::path canonical = fs::weakly_canonical(candidate, ec2);
            return (ec2 ? candidate : canonical).string();
        }
        if (candidate.filename() == "GTADATA") {
            // Maybe the caller passed the directory that contains GTADATA.
            fs::path nested = candidate / "GTADATA";
            if (looksLikeDataDir(nested)) return nested.string();
        }
    }
    return {};
}

bool AssetManager::load(const std::string& dataDir, const std::string& city,
                        std::string& error) {
    dataDir_ = findDataDir(dataDir);
    if (dataDir_.empty()) {
        error = "GTA data directory not found. Use --data <path-to-GTADATA> or set GTA_DATA_DIR.";
        return false;
    }

    std::string wanted = city.empty() ? "NYC" : upper(city);
    if (wanted.size() > 4 && wanted.substr(wanted.size() - 4) == ".CMP") {
        wanted = wanted.substr(0, wanted.size() - 4);
    }
    fs::path cmpPath = fs::path(dataDir_) / (wanted + ".CMP");
    std::error_code ec;
    if (!fs::exists(cmpPath, ec)) {
        // Accept common alternative names.
        if (wanted == "SANB" || wanted == "SANANDREAS") {
            cmpPath = fs::path(dataDir_) / "SANB.CMP";
        } else if (wanted == "MIAMI" || wanted == "VICE") {
            cmpPath = fs::path(dataDir_) / "MIAMI.CMP";
        } else if (wanted == "NYC" || wanted == "LIBERTY") {
            cmpPath = fs::path(dataDir_) / "NYC.CMP";
        }
    }
    if (!fs::exists(cmpPath, ec)) {
        error = "city file not found: " + cmpPath.string();
        return false;
    }
    cityName_ = upper(cmpPath.stem().string());

    if (!loadCityMap(cmpPath.string(), map_, error)) return false;

    const int style = map_.style >= 1 && map_.style <= 3 ? map_.style : 1;
    char styleName[32];
    std::snprintf(styleName, sizeof(styleName), "STYLE%03d.GRY", style);
    fs::path gryPath = fs::path(dataDir_) / styleName;
    if (!fs::exists(gryPath, ec)) {
        error = "graphics file not found: " + gryPath.string();
        return false;
    }
    if (!loadGry(gryPath.string(), gry_, error)) return false;
    if (gry_.animationDataTruncated) {
        std::fprintf(stderr,
                     "warning: %s declares %u tile animations but only %zu complete records fit; playing the complete records\n",
                     gryPath.filename().string().c_str(),
                     static_cast<unsigned>(gry_.declaredAnimationCount),
                     gry_.animations.size());
    }

    classifyRoadLids();
    if (!buildAtlas(error)) return false;
    if (!buildMinimap(error)) return false;
    spriteCache_.assign(gry_.sprites.size(), CachedSprite{});
    return true;
}

void AssetManager::classifyRoadLids() {
    const int count = gry_.numLid();
    lidIsRoad_.assign(static_cast<size_t>(count), 0);
    lidIsStructural_.assign(static_cast<size_t>(count), 0);
    lidIsWater_.assign(static_cast<size_t>(count), 0);
    lidIsGrass_.assign(static_cast<size_t>(count), 0);
    lidR_.assign(static_cast<size_t>(count), 0);
    lidG_.assign(static_cast<size_t>(count), 0);
    lidB_.assign(static_cast<size_t>(count), 0);
    if (count <= 1) return;

    // Usage count of each lid on the current map, used to pick the map's own
    // plain asphalt tile for the clean-road renderer instead of an arbitrary
    // dark tile (rail yards and roofs can be dark too).
    std::vector<uint32_t> lidUsage(static_cast<size_t>(count), 0);
    for (int y = 0; y < CityMap::kSize; ++y) {
        for (int x = 0; x < CityMap::kSize; ++x) {
            const int n = map_.blockCount(x, y);
            for (int z = n - 1; z >= 0; --z) {
                const MapBlock* block = map_.blockAt(x, y, z);
                if (!block || block->isAir() || block->isFlat() ||
                    block->lid == 0)
                    continue;
                if (block->lid < count) ++lidUsage[block->lid];
                break;
            }
        }
    }

    std::vector<uint8_t> tile(4096);
    int roads = 0;
    int grass = 0;
    uint32_t plainestUsage = 0;
    for (int lid = 1; lid < count; ++lid) {
        if (!gry_.tileData(gry_.lidSlot(lid), tile.data())) continue;

        int brightness = 0;
        int redSum = 0, greenSum = 0, blueSum = 0;
        int green = 0;
        int blue = 0;
        int yellow = 0;
        int structure = 0;
        for (uint8_t index : tile) {
            const int r = gry_.palette.rgb[index * 3 + 0];
            const int g = gry_.palette.rgb[index * 3 + 1];
            const int b = gry_.palette.rgb[index * 3 + 2];
            brightness += (r + g + b) / 3;
            redSum += r;
            greenSum += g;
            blueSum += b;
            if (g > r + 15 && g > b + 15) ++green;
            if (b > r + 20 && b > g + 10) ++blue;
            if (r > 130 && g > 100 && b < 90) ++yellow;
        }
        const int mean = brightness / 4096;
        const int meanR = redSum / 4096;
        const int meanG = greenSum / 4096;
        const int meanB = blueSum / 4096;
        lidR_[static_cast<size_t>(lid)] =
            static_cast<uint8_t>(std::clamp(meanR, 0, 255));
        lidG_[static_cast<size_t>(lid)] =
            static_cast<uint8_t>(std::clamp(meanG, 0, 255));
        lidB_[static_cast<size_t>(lid)] =
            static_cast<uint8_t>(std::clamp(meanB, 0, 255));

        // Asphalt is a dark, low-saturation tile.  Lane paint must not be
        // required: GTA composes a road from alternating plain-asphalt and
        // marked-asphalt cells, so testing for yellow pixels classifies half
        // of every lane as "not a road" (broken traffic, collision and map).
        const bool greenish = green > 900 && green > blue * 2 && meanG > meanR;
        const bool blueish = blue > 1200 && blue > green + 150;
        const int spread = std::max({meanR, meanG, meanB}) -
                           std::min({meanR, meanG, meanB});
        // Asphalt is neutral grey: brown planters (R noticeably above G) and
        // warm brick roofs must not be treated as road.
        const bool neutral = std::abs(meanR - meanG) <= 10 &&
                             meanG - meanB <= 25 && meanB <= meanG + 5;
        if (blueish && mean > 55) {
            lidIsWater_[static_cast<size_t>(lid)] = 1;
            if (waterFallbackLid_ == 1) waterFallbackLid_ = lid;
        }
        if (!greenish && !blueish && neutral && mean >= 45 && mean < 93 &&
            spread < 26) {
            if (structure > 4096 / 16) { // >6% structural pixels
                lidIsStructural_[static_cast<size_t>(lid)] = 1;
            }
            lidIsRoad_[static_cast<size_t>(lid)] = 1;
            if (roads == 0) roadFallbackLid_ = lid;
            ++roads;
            // A plain asphalt candidate: little lane paint and an asphalt-like
            // dark grey average; prefer the tile the map uses most.
            if (yellow < 30 && spread < 26 && mean >= 45 &&
                lidUsage[static_cast<size_t>(lid)] >= plainestUsage) {
                plainestUsage = lidUsage[static_cast<size_t>(lid)];
                plainRoadLid_ = lid;
            }
        } else if (roofFallbackLid_ == 1 && mean >= 96 && !greenish &&
                   !blueish) {
            // First neutral grey/concrete tile makes a reasonable roof.
            roofFallbackLid_ = lid;
        }
        if (green > 1200 && green > blue * 2 && mean > 35) {
            lidIsGrass_[static_cast<size_t>(lid)] = 1;
            if (grass == 0) grassFallbackLid_ = lid;
            ++grass;
        }
    }

    if (roads == 0) {
        // Conservative fallback for an unusual custom style.
        for (int lid : {1, 2, 3, 4, 5, 6}) {
            if (lid < count) lidIsRoad_[static_cast<size_t>(lid)] = 1;
        }
        roadFallbackLid_ = 1;
    }
    if (grass == 0) grassFallbackLid_ = 10 < count ? 10 : 1;
    if (waterFallbackLid_ == 1) waterFallbackLid_ = 1;
}

bool AssetManager::isStructuralLid(int index1Based) const {
    return index1Based > 0 &&
           index1Based < static_cast<int>(lidIsStructural_.size()) &&
           lidIsStructural_[static_cast<size_t>(index1Based)] != 0;
}

bool AssetManager::isRoadTile(int index1Based) const {
    if (index1Based <= 0 ||
        index1Based >= static_cast<int>(lidIsRoad_.size()))
        return false;
    return lidIsRoad_[static_cast<size_t>(index1Based)] != 0;
}

bool AssetManager::isWaterTile(int index1Based) const {
    if (index1Based <= 0 ||
        index1Based >= static_cast<int>(lidIsWater_.size()))
        return false;
    return lidIsWater_[static_cast<size_t>(index1Based)] != 0;
}

bool AssetManager::isGrassTile(int index1Based) const {
    if (index1Based <= 0 ||
        index1Based >= static_cast<int>(lidIsGrass_.size()))
        return false;
    return lidIsGrass_[static_cast<size_t>(index1Based)] != 0;
}

void AssetManager::lidColor(int index1Based, uint8_t& r, uint8_t& g,
                            uint8_t& b) const {
    if (index1Based <= 0 || index1Based >= static_cast<int>(lidR_.size())) {
        r = g = b = 0;
        return;
    }
    r = lidR_[static_cast<size_t>(index1Based)];
    g = lidG_[static_cast<size_t>(index1Based)];
    b = lidB_[static_cast<size_t>(index1Based)];
}

bool AssetManager::buildMinimap(std::string& error) {
    const int size = CityMap::kSize;
    std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4, 0);

    // GTA1's map screen is a clean, flat rendering of the city: asphalt roads,
    // blue water, green parks and grey roofs.  Rebuild that look directly
    // from the CMP/GRY data instead of dumping the noisy gameplay texture.
    const int groundLevel = [&]() {
        int histogram[8] = {0};
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const int count = map_.blockCount(x, y);
                int top = 0;
                for (int z = count - 1; z >= 0; --z) {
                    const MapBlock* block = map_.blockAt(x, y, z);
                    if (!block || block->isAir() || block->isFlat() ||
                        block->lid == 0)
                        continue;
                    top = z + 1;
                    break;
                }
                if (top > 0 && top < 8) ++histogram[top];
            }
        }
        int best = 2;
        for (int i = 1; i < 8; ++i) {
            if (histogram[i] > histogram[best]) best = i;
        }
        return best;
    }();

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const int count = map_.blockCount(x, y);
            int top = 0;
            int lid = 0;
            int surfaceType = -1;
            bool water = false;
            for (int z = count - 1; z >= 0; --z) {
                const MapBlock* block = map_.blockAt(x, y, z);
                if (!block || block->isAir() || block->isFlat() ||
                    block->lid == 0)
                    continue;
                top = z + 1;
                lid = block->lid;
                // CMP usually stores its surface class above the visible lid.
                // Some road cells carry type 2 on the lid itself; preserve
                // that explicit surface tag over unrelated stacked metadata.
                const MapBlock* above = map_.blockAt(x, y, z + 1);
                if (block->blockType() == 2)
                    surfaceType = 2;
                else if (above && above->blockType() != 0)
                    surfaceType = above->blockType();
                else
                    surfaceType = block->blockType();
                water = surfaceType == 1 ||
                        (surfaceType <= 0 && isWaterTile(block->lid));
                break;
            }

            uint8_t r = 30, g = 34, b = 30;
            if (water) {
                r = 34; g = 92; b = 158;
            } else if (top == 0) {
                r = 24; g = 56; b = 96; // open sea outside the map blocks
            } else if (surfaceType == 2 ||
                       (surfaceType <= 0 && isRoadTile(lid))) {
                r = 66; g = 68; b = 74;
            } else if (surfaceType == 4 ||
                       (surfaceType <= 0 && isGrassTile(lid))) {
                r = 74; g = 112; b = 56;
            } else if (surfaceType == 5 ||
                       (surfaceType <= 0 && top > groundLevel + 1)) {
                // Roof: use the tile's own average colour, slightly darkened.
                lidColor(lid, r, g, b);
                r = static_cast<uint8_t>(r * 3 / 4 + 30);
                g = static_cast<uint8_t>(g * 3 / 4 + 30);
                b = static_cast<uint8_t>(b * 3 / 4 + 30);
            } else {
                // Pavement.
                r = 148; g = 146; b = 138;
            }

            const size_t o = (static_cast<size_t>(y) * size + x) * 4;
            pixels[o + 0] = r;
            pixels[o + 1] = g;
            pixels[o + 2] = b;
            pixels[o + 3] = 255;
        }
    }

    glGenTextures(1, &minimap_);
    if (!minimap_) {
        error = "failed to create the minimap texture";
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, minimap_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

bool AssetManager::buildAtlas(std::string& error) {
    sideSlots_ = gry_.numSide();
    lidSlots_ = gry_.numLid();
    const int slots = gry_.totalSlots();
    if (slots <= 0) {
        error = "graphics archive has no tiles";
        return false;
    }

    // Four atlas variants let each CMP lid remap selector resolve through its
    // own GRY remap_index row. Side faces are never remapped by CMP.
    constexpr int kVariants = 4;
    const int cell = 64;
    const int cols = 16;
    const int rows = (slots + cols - 1) / cols;
    variantStride_ = cols * cell;
    atlasW_ = cols * cell * kVariants;
    atlasH_ = (rows + 1) * cell;
    std::vector<uint8_t> pixels(static_cast<size_t>(atlasW_) * atlasH_ * 4, 0);

    // One 8x8 white patch in the spare row feeds the procedural road paint.
    {
        const int wy = rows * cell;
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                const size_t o =
                    (static_cast<size_t>(wy + y) * atlasW_ + x) * 4;
                pixels[o + 0] = 255;
                pixels[o + 1] = 255;
                pixels[o + 2] = 255;
                pixels[o + 3] = 255;
            }
        }
        whiteUV_.u0 = 0.5f / atlasW_;
        whiteUV_.v0 = (wy + 0.5f) / atlasH_;
        whiteUV_.u1 = 7.5f / atlasW_;
        whiteUV_.v1 = (wy + 7.5f) / atlasH_;
    }

    sideUVs_.resize(sideSlots_ + 1);
    lidUVs_.resize(lidSlots_ + 1);
    auxUVs_.resize(static_cast<size_t>(std::max(0, slots - sideSlots_ - lidSlots_)));
    slotHasAlpha_.assign(static_cast<size_t>(slots), 0);

    std::vector<uint8_t> tile(4096);
    auto placeTile = [&](int slot) {
        if (!gry_.tileData(slot, tile.data())) return;
        for (int variant = 0; variant < kVariants; ++variant) {
            const int cx = (slot % cols) * cell + variant * variantStride_;
            const int cy = (slot / cols) * cell;
            const uint8_t* row = nullptr;
            if (variant > 0 && slot >= sideSlots_) {
                const int lidIndex = slot - sideSlots_;
                const uint8_t tableId = gry_.lidRemapTable(lidIndex, variant);
                const size_t tableOffset = static_cast<size_t>(tableId) * 256;
                if (tableId > 0 && gry_.remapTables.size() >= tableOffset + 256)
                    row = gry_.remapTables.data() + tableOffset;
            }
            for (int y = 0; y < cell; ++y) {
                for (int x = 0; x < cell; ++x) {
                    const uint8_t raw = tile[static_cast<size_t>(y) * cell + x];
                    const uint8_t index = row ? row[raw] : raw;
                    const size_t o =
                        (static_cast<size_t>(cy + y) * atlasW_ + cx + x) * 4;
                    pixels[o + 0] = gry_.palette.rgb[index * 3 + 0];
                    pixels[o + 1] = gry_.palette.rgb[index * 3 + 1];
                    pixels[o + 2] = gry_.palette.rgb[index * 3 + 2];
                    // Palette index 0 is the transparency key for sprites and
                    // for the "flat" map decals (trees, road markings, ...).
                    pixels[o + 3] = raw == 0 ? 0 : 255;
                }
            }
            if (variant == 0) {
                const float inset = 0.5f;
                TileUV uv;
                uv.u0 = (cx + inset) / atlasW_;
                uv.v0 = (cy + inset) / atlasH_;
                uv.u1 = (cx + cell - inset) / atlasW_;
                uv.v1 = (cy + cell - inset) / atlasH_;
                if (slot >= sideSlots_ && slot < sideSlots_ + lidSlots_) {
                    lidUVs_[slot - sideSlots_] = uv;
                } else if (slot < sideSlots_) {
                    sideUVs_[slot] = uv;
                } else {
                    const size_t aux =
                        static_cast<size_t>(slot - sideSlots_ - lidSlots_);
                    if (aux < auxUVs_.size()) auxUVs_[aux] = uv;
                }
                // The transparency mask is the same for every palette
                // variant: palette index 0 is always the colour key.
                bool any = false;
                for (int yy = 0; yy < cell && !any; ++yy) {
                    for (int xx = 0; xx < cell; ++xx) {
                        if (tile[static_cast<size_t>(yy) * cell + xx] == 0) {
                            any = true;
                            break;
                        }
                    }
                }
                slotHasAlpha_[static_cast<size_t>(slot)] = any ? 1 : 0;
            }
        }
    };

    for (int slot = 0; slot < slots; ++slot) placeTile(slot);

    // A tile must stay in the alpha pass if any of its animation frames has
    // colour-key holes. Some shipped animations change alpha between frames
    // (STYLE002 lid 69 is opaque in the base tile and partly transparent in
    // two AUX frames), so classifying only the base image makes the holes draw
    // as opaque black.
    std::vector<uint8_t> animationTile(64 * 64);
    auto hasColourKey = [&](int slot) {
        if (!gry_.tileData(slot, animationTile.data())) return false;
        return std::find(animationTile.begin(), animationTile.end(), 0) !=
               animationTile.end();
    };
    for (const GryAnimation& animation : gry_.animations) {
        const int baseSlot = animation.which == 0
            ? gry_.sideSlot(animation.block)
            : gry_.lidSlot(animation.block);
        if (baseSlot < 0 || baseSlot >= slots) continue;
        bool transparent = slotHasAlpha_[static_cast<size_t>(baseSlot)] != 0;
        for (uint8_t frame : animation.frames) {
            const int frameSlot = gry_.auxSlot(frame);
            transparent = transparent || hasColourKey(frameSlot);
        }
        slotHasAlpha_[static_cast<size_t>(baseSlot)] = transparent ? 1 : 0;
    }

    glGenTextures(1, &atlas_);
    if (!atlas_) {
        error = "failed to create OpenGL atlas texture";
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, atlas_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, atlasW_, atlasH_, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, pixels.data());
    // Bilinear filtering keeps the 64x64 asphalt tiles from shimmering when
    // the fixed-angle camera stretches them; mipmaps are avoided because the
    // atlas has no padding between tiles.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    animationCycles_ = 0.0;
    animationPhases_.assign(gry_.animations.size(), 0);
    return true;
}

void AssetManager::updateAnimations(float dt) {
    if (!atlas_ || dt <= 0.0f || gry_.animations.empty()) return;

    // The retail animation speed is expressed in game cycles. Map those cycles
    // to a 30 Hz compatibility clock so playback stays stable across desktop
    // and mobile render rates; verify the exact cadence against the DOS build.
    constexpr double kCyclesPerSecond = 30.0;
    animationCycles_ += static_cast<double>(dt) * kCyclesPerSecond;

    for (size_t i = 0; i < gry_.animations.size(); ++i) {
        const GryAnimation& animation = gry_.animations[i];
        const size_t phaseCount = animation.frames.size() + 1;
        const double cyclesPerFrame = std::max<int>(1, animation.speed);
        const int phase = static_cast<int>(
            std::fmod(std::floor(animationCycles_ / cyclesPerFrame),
                      static_cast<double>(phaseCount)));
        if (phase == animationPhases_[i]) continue;
        uploadAnimatedTile(animation, phase);
        animationPhases_[i] = static_cast<uint8_t>(phase);
    }
}

void AssetManager::uploadAnimatedTile(const GryAnimation& animation, int phase) {
    if (!atlas_ || animation.which > 1 || phase < 0 ||
        phase > static_cast<int>(animation.frames.size()))
        return;

    const int sourceSlot = phase == 0
        ? (animation.which == 0 ? gry_.sideSlot(animation.block)
                               : gry_.lidSlot(animation.block))
        : gry_.auxSlot(animation.frames[static_cast<size_t>(phase - 1)]);
    const int baseSlot = animation.which == 0
        ? gry_.sideSlot(animation.block)
        : gry_.lidSlot(animation.block);
    const int baseTileCount = animation.which == 0 ? gry_.numSide()
                                                   : gry_.numLid();
    if (animation.block == 0 || animation.block > baseTileCount ||
        baseSlot < 0 || baseSlot >= gry_.numSide() + gry_.numLid() ||
        sourceSlot < 0 || sourceSlot >= gry_.totalSlots())
        return;

    std::vector<uint8_t> indices(64 * 64);
    if (!gry_.tileData(sourceSlot, indices.data())) return;
    std::vector<uint8_t> rgba(64 * 64 * 4);
    const size_t tableBytes = 256;
    glBindTexture(GL_TEXTURE_2D, atlas_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int variant = 0; variant < 4; ++variant) {
        const uint8_t* remap = nullptr;
        if (variant > 0 && animation.which == 1) {
            const uint8_t tableId = gry_.lidRemapTable(animation.block, variant);
            const size_t tableOffset = static_cast<size_t>(tableId) * tableBytes;
            if (tableId > 0 && gry_.remapTables.size() >= tableOffset + tableBytes)
                remap = gry_.remapTables.data() + tableOffset;
        }
        for (size_t pixel = 0; pixel < indices.size(); ++pixel) {
            const uint8_t source = indices[pixel];
            const uint8_t color = remap ? remap[source] : source;
            const size_t out = pixel * 4;
            rgba[out + 0] = gry_.palette.rgb[static_cast<size_t>(color) * 3 + 0];
            rgba[out + 1] = gry_.palette.rgb[static_cast<size_t>(color) * 3 + 1];
            rgba[out + 2] = gry_.palette.rgb[static_cast<size_t>(color) * 3 + 2];
            rgba[out + 3] = source == 0 ? 0 : 255;
        }
        const int x = (baseSlot % 16) * 64 + variant * variantStride_;
        const int y = (baseSlot / 16) * 64;
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, 64, 64, GL_RGBA,
                        GL_UNSIGNED_BYTE, rgba.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

TileUV AssetManager::remapped(TileUV uv, int remap) const {
    if (remap <= 0 || variantStride_ <= 0 || atlasW_ <= 0) return uv;
    const float shift = static_cast<float>(remap * variantStride_) /
                        static_cast<float>(atlasW_);
    uv.u0 += shift;
    uv.u1 += shift;
    return uv;
}

TileUV AssetManager::sideUV(int index1Based, int remap) const {
    (void)remap; // GRY remaps are authored for lids only.
    // Side tile N occupies atlas slot N (slot 0 is unused by the maps).
    const int i = index1Based;
    if (i <= 0) return TileUV{};
    const int slot = i;
    if (slot < sideSlots_) {
        return sideUVs_[static_cast<size_t>(slot)];
    }
    if (slot < sideSlots_ + lidSlots_) {
        return lidUVs_[static_cast<size_t>(slot - sideSlots_)];
    }
    const int aux = slot - sideSlots_ - lidSlots_;
    if (aux >= 0 && aux < static_cast<int>(auxUVs_.size())) {
        return auxUVs_[static_cast<size_t>(aux)];
    }
    return TileUV{};
}

TileUV AssetManager::lidUV(int index1Based, int remap) const {
    // Lid tile N occupies atlas slot numSide + N.  Some maps (SANB) use
    // indices beyond the lid section, which actually refer to the auxiliary
    // tile bank that follows it in the GRY archive.
    const int i = index1Based;
    if (i <= 0) return TileUV{};
    const int slot = sideSlots_ + i;
    if (slot < sideSlots_ + lidSlots_) {
        return remapped(lidUVs_[static_cast<size_t>(slot - sideSlots_)], remap);
    }
    const int aux = slot - sideSlots_ - lidSlots_;
    if (aux >= 0 && aux < static_cast<int>(auxUVs_.size())) {
        return remapped(auxUVs_[static_cast<size_t>(aux)], remap);
    }
    return TileUV{};
}

bool AssetManager::sideHasAlpha(int index1Based) const {
    if (index1Based <= 0 ||
        index1Based >= static_cast<int>(slotHasAlpha_.size()))
        return false;
    return slotHasAlpha_[static_cast<size_t>(index1Based)] != 0;
}

bool AssetManager::lidHasAlpha(int index1Based) const {
    const int slot = sideSlots_ + index1Based;
    if (index1Based <= 0 || slot >= static_cast<int>(slotHasAlpha_.size()))
        return false;
    return slotHasAlpha_[static_cast<size_t>(slot)] != 0;
}

GLuint AssetManager::spriteTexture(int grySpriteIndex) {
    if (grySpriteIndex < 0 ||
        grySpriteIndex >= static_cast<int>(spriteCache_.size())) {
        return 0;
    }
    CachedSprite& cached = spriteCache_[grySpriteIndex];
    if (cached.texture) return cached.texture;

    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    if (!gry_.spriteRGBA(grySpriteIndex, rgba, w, h)) return 0;

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    cached.texture = id;
    cached.w = w;
    cached.h = h;
    return id;
}

int AssetManager::spriteWidth(int grySpriteIndex) const {
    if (grySpriteIndex < 0 ||
        grySpriteIndex >= static_cast<int>(spriteCache_.size()))
        return 0;
    return spriteCache_[grySpriteIndex].w;
}

int AssetManager::spriteHeight(int grySpriteIndex) const {
    if (grySpriteIndex < 0 ||
        grySpriteIndex >= static_cast<int>(spriteCache_.size()))
        return 0;
    return spriteCache_[grySpriteIndex].h;
}

void AssetManager::destroy() {
    for (CachedSprite& s : spriteCache_) {
        if (s.texture) {
            glDeleteTextures(1, &s.texture);
            s.texture = 0;
        }
    }
    spriteCache_.clear();
    if (atlas_) {
        glDeleteTextures(1, &atlas_);
        atlas_ = 0;
    }
    if (minimap_) {
        glDeleteTextures(1, &minimap_);
        minimap_ = 0;
    }
}

AssetManager::~AssetManager() { destroy(); }

} // namespace gta
