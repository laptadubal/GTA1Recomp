#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gta {

// ---------------------------------------------------------------------------
// CMP - compressed city map, format version 331
// ---------------------------------------------------------------------------

struct MapBlock {
    uint16_t typeMap = 0;
    uint8_t typeMapExt = 0;
    uint8_t left = 0;
    uint8_t right = 0;
    uint8_t top = 0;
    uint8_t bottom = 0;
    uint8_t lid = 0;

    bool isAir() const {
        return typeMap == 0 && typeMapExt == 0 && left == 0 && right == 0 &&
               top == 0 && bottom == 0 && lid == 0;
    }
    uint8_t blockType() const {
        return static_cast<uint8_t>((typeMap & 16 ? 1 : 0) + (typeMap & 32 ? 2 : 0) +
                                    (typeMap & 64 ? 4 : 0));
    }
    // Direction permissions for computer-controlled road traffic:
    // bit 0 north (-Y), bit 1 south (+Y), bit 2 west (-X), bit 3 east (+X).
    uint8_t directionMask() const { return static_cast<uint8_t>(typeMap & 15); }
    bool isFlat() const { return (typeMap & 128) != 0; }
    // 0 = flat cube, 1-40 = 7/26-degree ramps, 41-44 = 45-degree ramps.
    uint8_t slopeType() const {
        return static_cast<uint8_t>((typeMap & 256 ? 1 : 0) +
                                    (typeMap & 512 ? 2 : 0) +
                                    (typeMap & 1024 ? 4 : 0) +
                                    (typeMap & 2048 ? 8 : 0) +
                                    (typeMap & 4096 ? 16 : 0) +
                                    (typeMap & 8192 ? 32 : 0));
    }
    uint8_t rotation() const {
        return static_cast<uint8_t>((typeMap & 16384 ? 1 : 0) + (typeMap & 32768 ? 2 : 0));
    }
    // Lid remap selector (0 = none, 1..3 = lookup via GRY remap_index).
    uint8_t remapIndex() const {
        return static_cast<uint8_t>((typeMapExt >> 3) & 3);
    }
    bool flipTopBottom() const { return (typeMapExt & 32) != 0; }
    bool flipLeftRight() const { return (typeMapExt & 64) != 0; }
};

// Named district stored in the CMP navigation table.  Each record is 35 bytes:
// x, y, width, height, an FXT sample number and a 30-byte name field.  The
// engine resolves the display name from ENGLISH.FXT instead.
struct MapSector {
    uint8_t x = 0, y = 0, w = 0, h = 0;
    uint8_t sample = 0;
    std::string name;
};

struct MapObject {
    uint16_t x = 0, y = 0, z = 0; // 8.8 fixed point
    uint8_t type = 0;
    uint8_t remap = 0;
    uint16_t rotation = 0, pitch = 0, roll = 0;
};

struct MapRouteVertex {
    uint8_t x = 0, y = 0, z = 0;
};

struct MapRoute {
    uint8_t type = 0;
    std::vector<MapRouteVertex> vertices;
};

struct CityMap {
    static constexpr int kSize = 256;
    // Every CMP column holds at most six 1-unit levels.
    static constexpr int kMaxLevels = 6;

    uint32_t version = 0;
    uint8_t style = 0;
    uint8_t sample = 0;
    uint32_t routeSize = 0, objectPosSize = 0, columnSize = 0, blockSize = 0,
             navDataSize = 0;

    std::vector<uint32_t> base;    // 256 * 256 offsets into column[]
    std::vector<uint16_t> column;  // block columns
    std::vector<MapBlock> blocks;
    std::vector<MapObject> objects;
    std::vector<MapRoute> routes;
    std::vector<MapSector> sectors;

    bool valid() const { return version == 331 && base.size() == kSize * kSize; }

    // The base grid is stored column-major with respect to the map axes used
    // everywhere else in the game (CMP objects, MISSION.INI payphones and the
    // CMP route network all address a cell as base[y * kSize + x]).  Reading it
    // as base[x * kSize + y] transposes the whole city: the geometry is still
    // city-shaped, but every street, block and landmark is mirrored about the
    // map diagonal, so the result never matches the original layout.  Verified
    // against all 128 Liberty City payphones in MISSION.INI: with this index
    // every one lands on a surface at exactly its authored height, with the
    // other index 39 of them sit inside buildings.
    int blockCount(int x, int y) const {
        if (x < 0 || y < 0 || x >= kSize || y >= kSize) return 0;
        uint32_t idx = base[y * kSize + x] / 2;
        if (idx >= column.size()) return 0;
        // CMP stores the number of *empty* levels above the column, not the
        // number of blocks.  GTA1 columns are six levels tall, so a stored
        // value of 3 means three blocks and a value of 0 means six.
        const int empty = column[idx];
        const int n = kMaxLevels - empty;
        if (n <= 0) return 0;
        if (idx + static_cast<uint32_t>(n) >= column.size()) return 0;
        return n;
    }

    // z == 0 is the lowest block of the column.
    const MapBlock* blockAt(int x, int y, int z) const {
        int n = blockCount(x, y);
        if (z < 0 || z >= n) return nullptr;
        uint32_t idx = base[y * kSize + x] / 2;
        // entries after the empty-level count are stored top-to-bottom
        uint16_t blockIndex = column[idx + n - z];
        if (blockIndex >= blocks.size()) return nullptr;
        return &blocks[blockIndex];
    }
};

bool loadCityMap(const std::string& path, CityMap& out, std::string& error);

// ---------------------------------------------------------------------------
// GRY - 8-bit graphics archive, format version 325
// ---------------------------------------------------------------------------

struct GryCarInfo {
    int16_t width = 0, height = 0, depth = 0;
    int16_t sprNum = 0;
    int16_t weightDescriptor = 0;
    int16_t maxSpeed = 0, minSpeed = 0;
    int16_t acceleration = 0, braking = 0;
    int16_t grip = 0, handling = 0;
    uint8_t model = 0;
    uint8_t vtype = 0;
    uint8_t turning = 0;
    uint8_t damagable = 0;
    uint8_t remap8[12] = {};
};

struct GryObjectInfo {
    uint32_t width = 0, height = 0, depth = 0;
    uint16_t sprNum = 0;
    uint16_t weight = 0;
    uint16_t aux = 0;
    int8_t status = 0;
    uint8_t numInto = 0;
};

struct GrySpriteInfo {
    uint8_t w = 0;
    uint8_t h = 0;
    uint8_t deltaCount = 0;
    uint8_t flags = 0;
    uint16_t size = 0;
    uint16_t clut = 0;
    uint8_t xoffset = 0;
    uint8_t yoffset = 0;
    uint16_t page = 0;
    struct Delta {
        uint16_t size = 0;
        uint32_t ptr = 0;
    };
    std::vector<Delta> deltas;
};

struct Palette {
    // 768 bytes RGB, expanded from 6-bit VGA to 8-bit.
    uint8_t rgb[768] = {};
    void apply(const uint8_t* indices, int count, uint8_t* rgba, bool transparentZero) const;
};

struct GryAnimation {
    uint8_t block = 0;       // original side/lid tile number (1-based)
    uint8_t which = 0;       // 0 = side, 1 = lid
    uint8_t speed = 0;       // game cycles per frame
    std::vector<uint8_t> frames; // 1-based auxiliary tile numbers
};

struct GryFile {
    uint32_t version = 0;
    uint32_t sideSize = 0, lidSize = 0, auxSize = 0, animSize = 0;
    uint32_t paletteSize = 0, remapSize = 0, remapIndexSize = 0;
    uint32_t objectInfoSize = 0, carInfoSize = 0, spriteInfoSize = 0,
             spriteGraphicsSize = 0, spriteNumberSize = 0;

    // Raw tile bytes for side + lid + aux tiles, exactly as stored in the
    // archive.  Tiles are interleaved in groups of four: each tile row is 64
    // bytes, rows of the four tiles alternate, and each group occupies 16 KiB.
    std::vector<uint8_t> rawTiles;
    Palette palette;
    std::vector<uint8_t> remapTables; // 256 * 256
    std::vector<uint8_t> remapIndexTable; // 256 lids * four selectors
    std::vector<GryAnimation> animations;
    uint8_t declaredAnimationCount = 0;
    bool animationDataTruncated = false;
    std::vector<uint8_t> spriteGraphics;
    std::vector<GrySpriteInfo> sprites;
    std::vector<GryObjectInfo> objectsInfo;
    std::vector<GryCarInfo> cars;
    uint16_t spriteNumbers[21] = {};

    int numSide() const { return static_cast<int>(sideSize / 4096); }
    int numLid() const { return static_cast<int>(lidSize / 4096); }
    int numAux() const { return static_cast<int>(auxSize / 4096); }

    // The tile sections are stored back to back.  Map indices are 1-based:
    // side tile N lives in slot N, lid tile N in slot numSide() + N.
    int sideSlot(int index1Based) const { return index1Based; }
    int lidSlot(int index1Based) const { return numSide() + index1Based; }
    int auxSlot(int index1Based) const { return numSide() + numLid() + index1Based; }
    uint8_t lidRemapTable(int lidIndex1Based, int selector) const {
        if (lidIndex1Based < 0 || lidIndex1Based > 255 || selector < 0 ||
            selector > 3)
            return 0;
        const size_t index = static_cast<size_t>(lidIndex1Based) * 4 + selector;
        return index < remapIndexTable.size() ? remapIndexTable[index] : 0;
    }
    int totalSlots() const {
        return static_cast<int>(rawTiles.size() / 4096);
    }

    bool valid() const { return version == 325 && !rawTiles.empty(); }

    // Decodes one 64x64 tile (palette indices) from the interleaved storage.
    bool tileData(int slot, uint8_t* dst) const;

    // Converts a sprite (base frame) to RGBA8.
    bool spriteRGBA(int index, std::vector<uint8_t>& rgba, int& w, int& h) const;
    // Raw palette indices (no palette expansion), needed for remapped
    // sprite variants (pedestrian/character clothing variants live in the
    // GRY remap tables).
    bool spritePixels(int index, std::vector<uint8_t>& pixels, int& w,
                      int& h) const;

    // Absolute sprite index for a car model's sprite number.
    int carSpriteIndex(int carSpriteNum) const {
        return spriteNumbers[0] + spriteNumbers[1] + spriteNumbers[2] +
               spriteNumbers[3] + spriteNumbers[4] + carSpriteNum;
    }

    // Absolute sprite index for a map object type.
    int objectSpriteIndex(int objectType) const {
        if (objectType < 0 ||
            objectType >= static_cast<int>(objectsInfo.size()))
            return -1;
        int base = 0;
        for (int i = 0; i < 6; ++i) base += spriteNumbers[i];
        return base + objectsInfo[static_cast<size_t>(objectType)].sprNum;
    }

    // First sprite index of a group.  The GRY tail stores the sprite count
    // of every group, laid out in the original SpriteTypes order
    // (ARROW=0, DIGIT=1, BOAT=2, BOX=3, BUS=4, CAR=5, OBJECT=6, PED=7,
    //  SPEEDO=8, TANK=9, TRAFFIC_LIGHT=10, TRAIN=11, TRDOOR=12, BIKE=13,
    //  TRAM=14, WBUS=15, WCAR=16, EX=17, TUMCAR=18, TUMTRUCK=19, FERRY=20);
    // the group's index is the sum of the earlier groups' counts.  Group
    // sizes differ between cities (e.g. MIAMI has extra TRAM/bike sprites),
    // so absolute indexes like "954 = fire" must not be hardcoded.
    int spriteGroupBase(int type) const {
        int base = 0;
        for (int i = 0; i < type && i < 21; ++i)
            base += spriteNumbers[static_cast<size_t>(i)];
        return base;
    }
    int wcarSpriteBase() const { return spriteGroupBase(16); } // 7 fire frames
    int explosionSpriteBase() const { return spriteGroupBase(17); } // 48
};

bool loadGry(const std::string& path, GryFile& out, std::string& error);

} // namespace gta
