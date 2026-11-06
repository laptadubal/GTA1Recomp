// Smoke tests for the CMP and GRY parsers.  They run against the extracted
// game data and are skipped gracefully when the data directory is unavailable.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "data/formats.h"

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

std::string findDataDir() {
#ifdef GTA_DEFAULT_DATA_DIR
    return GTA_DEFAULT_DATA_DIR;
#else
    return "game_data/GTADATA";
#endif
}

// The shipped cube/slope models, kept in the renderer so the world mesh uses
// exactly the original geometry instead of a re-invented ramp shape.
const float kSlopeRaw[45][5][4][3] = {
#include "game/slope_raw.inc"
};

const char* kDirectionNames[4] = {"north", "south", "west", "east"};
// Empirical CMP field mapping, verified over every exposed face of all three
// cities: north/left, south/right, west/top, east/bottom.
const int kDirectionFields[4] = {2, 3, 4, 5}; // 2=left, 3=right, 4=top, 5=bottom

int fieldOf(const gta::MapBlock& block, int field) {
    switch (field) {
    case 2: return block.left;
    case 3: return block.right;
    case 4: return block.top;
    default: return block.bottom;
    }
}

int solidTop(const gta::CityMap& map, int x, int y) {
    if (x < 0 || y < 0 || x >= gta::CityMap::kSize ||
        y >= gta::CityMap::kSize)
        return 0;
    const int n = map.blockCount(x, y);
    int top = 0;
    for (int z = 0; z < n; ++z) {
        const gta::MapBlock* block = map.blockAt(x, y, z);
        if (!block || block->isAir() || block->isFlat() || block->lid == 0)
            continue;
        top = z + 1;
    }
    return top;
}

} // namespace

int main() {
    const std::string dataDir = findDataDir();
    const std::string cmpPath = dataDir + "/NYC.CMP";
    const std::string gryPath = dataDir + "/STYLE001.GRY";

    gta::CityMap map;
    std::string error;
    if (!gta::loadCityMap(cmpPath, map, error)) {
        std::fprintf(stderr, "skipping tests: %s\n", error.c_str());
        return 0;
    }
    check(map.valid(), "city map is valid");
    check(map.version == 331, "city map version is 331");
    check(map.style == 1, "NYC uses style 1");
    check(map.objects.size() > 1000, "city map contains objects");
    check(map.blocks.size() > 1000, "city map contains blocks");
    check(map.column.size() > 1000, "city map contains columns");

    int nonAir = 0;
    for (int y = 0; y < 256; ++y) {
        for (int x = 0; x < 256; ++x) {
            const int count = map.blockCount(x, y);
            check(count >= 0 && count <= 6, "block count in range");
            for (int z = 0; z < count; ++z) {
                const gta::MapBlock* block = map.blockAt(x, y, z);
                check(block != nullptr, "block lookup succeeds");
                if (block && !block->isAir()) ++nonAir;
            }
        }
    }
    check(nonAir > 100000, "city map contains a populated world");

    // The stored column value is the number of empty levels, so nearly every
    // cell must have a visible top.  Reading it as a block count truncates
    // ~40% of the city (the bug that made roads and roofs come from the
    // wrong level).
    int withTop = 0;
    int tall = 0;
    int maxTop = 0;
    for (int y = 0; y < 256; ++y) {
        for (int x = 0; x < 256; ++x) {
            const int count = map.blockCount(x, y);
            int top = 0;
            for (int z = count - 1; z >= 0; --z) {
                const gta::MapBlock* block = map.blockAt(x, y, z);
                if (!block || block->isAir() || block->isFlat() ||
                    block->lid == 0)
                    continue;
                top = z + 1;
                break;
            }
            if (top > 0) ++withTop;
            if (top >= 5) ++tall;
            if (top > maxTop) maxTop = top;
        }
    }
    check(withTop > 64000, "almost every cell has a visible surface");
    check(tall > 5000, "the city has real multi-storey buildings");
    check(maxTop == 6, "column heights reach the six-level maximum");

    gta::GryFile gry;
    if (!gta::loadGry(gryPath, gry, error)) {
        std::fprintf(stderr, "FAIL: %s\n", error.c_str());
        return 1;
    }
    check(gry.valid(), "graphics archive is valid");
    check(gry.version == 325, "graphics archive version is 325");
    check(gry.numSide() > 100, "side tile count");
    check(gry.numLid() > 100, "lid tile count");
    check(gry.sprites.size() > 500, "sprite count");
    check(gry.cars.size() == 38, "NYC car count is 38");

    uint8_t tile[4096];
    check(gry.tileData(1, tile), "can decode side tile 1");
    check(gry.tileData(gry.lidSlot(1), tile), "can decode lid tile 1");

    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    const int carSprite = gry.carSpriteIndex(gry.cars.front().sprNum);
    check(carSprite >= 0 && carSprite < static_cast<int>(gry.sprites.size()),
          "car sprite index in range");
    check(gry.spriteRGBA(carSprite, rgba, w, h), "can decode a car sprite");
    check(w > 0 && h > 0 && rgba.size() == static_cast<size_t>(w) * h * 4,
          "car sprite has RGBA pixels");

    // --- MapBlock bit decoding -------------------------------------------
    {
        gta::MapBlock block{};
        block.typeMap = 0;
        check(block.slopeType() == 0, "flat cube has slope type 0");
        block.typeMap = 1u << 8;
        check(block.slopeType() == 1, "slope bit 0 decodes to type 1");
        block.typeMap = 1u << 13;
        check(block.slopeType() == 32, "slope bit 5 decodes to type 32");
        block.typeMap = 0x3F00;
        check(block.slopeType() == 63, "all slope bits decode to 63");
        block.typeMap = 1u << 7;
        check(block.isFlat(), "flat bit decodes");
        block.typeMap = 1u << 14;
        check(block.rotation() == 1, "rotation bit 0 decodes");
        block.typeMap = 1u << 15;
        check(block.rotation() == 2, "rotation bit 1 decodes");
        block.typeMap = (1u << 14) | (1u << 15);
        check(block.rotation() == 3, "both rotation bits decode to 3");
    }

    // --- Original 45 slope models ----------------------------------------
    {
        int badVertex = 0;
        int badLidCorner = 0;
        for (int s = 0; s < 45; ++s) {
            for (int f = 0; f < 5; ++f) {
                for (int j = 0; j < 4; ++j) {
                    for (int k = 0; k < 3; ++k) {
                        const float v = kSlopeRaw[s][f][j][k];
                        if (v < -0.001f || v > 1.001f) ++badVertex;
                    }
                }
            }
            // Every original lid is ordered SW, SE, NE, NW so the shared UV
            // rotation can be applied uniformly to all 45 models.
            const float expectedX[4] = {0.0f, 1.0f, 1.0f, 0.0f};
            const float expectedZ[4] = {1.0f, 1.0f, 0.0f, 0.0f};
            for (int j = 0; j < 4; ++j) {
                if (kSlopeRaw[s][0][j][0] != expectedX[j] ||
                    kSlopeRaw[s][0][j][2] != expectedZ[j])
                    ++badLidCorner;
            }
        }
        check(badVertex == 0, "slope vertices stay inside one cube");
        check(badLidCorner == 0, "all 45 lids use SW,SE,NE,NW order");
    }

    // --- Known cell layout (elevated railway platforms) -------------------
    {
        const gta::MapBlock* deck = map.blockAt(118, 112, 4);
        check(deck != nullptr, "railway deck block exists");
        if (deck) {
            check(deck->lid == 42, "railway deck keeps its authored lid");
            check(!deck->isFlat(), "railway deck is opaque");
            check(deck->slopeType() == 0, "railway deck is a flat cube");
            check(deck->left == 113 && deck->top == 113,
                  "railway deck keeps its authored girder sides");
        }
        const gta::MapBlock* air = map.blockAt(118, 112, 0);
        check(air != nullptr && air->isAir(),
              "z-order: the level below the deck is empty air");
        const gta::MapBlock* fence = map.blockAt(112, 111, 3);
        check(fence != nullptr && fence->isFlat() && fence->left == 106,
              "flat BAR sign is stored one level above the pavement");
    }

    // --- Authored side coverage / empirical direction mapping -------------
    {
        int exposed[4] = {0, 0, 0, 0};
        int authored[4] = {0, 0, 0, 0};
        const int dx[4] = {0, 0, -1, 1};
        const int dy[4] = {-1, 1, 0, 0};
        for (int y = 1; y < gta::CityMap::kSize - 1; ++y) {
            for (int x = 1; x < gta::CityMap::kSize - 1; ++x) {
                const int top = solidTop(map, x, y);
                if (top < 2) continue;
                for (int f = 0; f < 4; ++f) {
                    const int neighbourTop =
                        solidTop(map, x + dx[f], y + dy[f]);
                    for (int z = neighbourTop; z < top; ++z) {
                        const gta::MapBlock* block = map.blockAt(x, y, z);
                        if (!block || block->isAir()) continue;
                        ++exposed[f];
                        if (fieldOf(*block, kDirectionFields[f]) != 0)
                            ++authored[f];
                    }
                }
            }
        }
        int totalExposed = 0;
        int totalAuthored = 0;
        for (int f = 0; f < 4; ++f) {
            totalExposed += exposed[f];
            totalAuthored += authored[f];
            const double ratio = exposed[f] > 0
                                     ? static_cast<double>(authored[f]) /
                                           static_cast<double>(exposed[f])
                                     : 0.0;
            if (ratio < 0.90) {
                std::fprintf(stderr,
                             "FAIL: %s authored-side coverage only %.1f%%\n",
                             kDirectionNames[f], ratio * 100.0);
                ++failures;
            }
        }
        check(totalExposed > 50000, "enough exposed faces for the mapping test");
        const double overall = totalExposed > 0
                                   ? static_cast<double>(totalAuthored) /
                                         static_cast<double>(totalExposed)
                                   : 0.0;
        check(overall > 0.93, "empirical N/S/W/E field mapping holds");
    }

    if (failures == 0) {
        std::printf("format tests passed (blocks=%zu, sprites=%zu, cars=%zu)\n",
                    map.blocks.size(), gry.sprites.size(), gry.cars.size());
        return 0;
    }    std::fprintf(stderr, "%d format test(s) failed\n", failures);
    return 1;
}
