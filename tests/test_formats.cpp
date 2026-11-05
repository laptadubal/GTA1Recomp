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

    if (failures == 0) {
        std::printf("format tests passed (blocks=%zu, sprites=%zu, cars=%zu)\n",
                    map.blocks.size(), gry.sprites.size(), gry.cars.size());
        return 0;
    }
    std::fprintf(stderr, "%d format test(s) failed\n", failures);
    return 1;
}
