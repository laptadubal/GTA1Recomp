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
    // Palette remap variant (0..3) from the extension flags; selects one of
    // the four remap rows in the GRY archive.
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
    std::vector<MapSector> sectors;

    bool valid() const { return version == 331 && base.size() == kSize * kSize; }

    int blockCount(int x, int y) const {
        if (x < 0 || y < 0 || x >= kSize || y >= kSize) return 0;
        uint32_t idx = base[x * kSize + y] / 2;
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
        uint32_t idx = base[x * kSize + y] / 2;
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
    int totalSlots() const {
        return static_cast<int>(rawTiles.size() / 4096);
    }

    bool valid() const { return version == 325 && !rawTiles.empty(); }

    // Decodes one 64x64 tile (palette indices) from the interleaved storage.
    bool tileData(int slot, uint8_t* dst) const;

    // Converts a sprite (base frame) to RGBA8.
    bool spriteRGBA(int index, std::vector<uint8_t>& rgba, int& w, int& h) const;

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
};

bool loadGry(const std::string& path, GryFile& out, std::string& error);

} // namespace gta
