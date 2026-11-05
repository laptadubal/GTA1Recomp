#include "data/formats.h"

#include <cstring>
#include <fstream>
#include <iterator>

namespace gta {
namespace {

uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t rd16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
int16_t rds16(const uint8_t* p) { return static_cast<int16_t>(rd16(p)); }

bool readFile(const std::string& path, std::vector<uint8_t>& out, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "cannot open file: " + path;
        return false;
    }
    f.seekg(0, std::ios::end);
    std::streamoff size = f.tellg();
    if (size <= 0) {
        error = "empty file: " + path;
        return false;
    }
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(out.data()), size);
    if (!f) {
        error = "short read: " + path;
        return false;
    }
    return true;
}

} // namespace

bool loadCityMap(const std::string& path, CityMap& out, std::string& error) {
    std::vector<uint8_t> data;
    if (!readFile(path, data, error)) return false;

    if (data.size() < 28) {
        error = "CMP file is too small";
        return false;
    }

    CityMap map;
    map.version = rd32(data.data() + 0);
    map.style = data[4];
    map.sample = data[5];
    map.routeSize = rd32(data.data() + 8);
    map.objectPosSize = rd32(data.data() + 12);
    map.columnSize = rd32(data.data() + 16);
    map.blockSize = rd32(data.data() + 20);
    map.navDataSize = rd32(data.data() + 24);

    if (map.version != 331) {
        error = "unsupported CMP version (expected 331, got " +
                std::to_string(map.version) + ")";
        return false;
    }

    const size_t headerSize = 28;
    const size_t baseSize = 256 * 256 * 4;
    if (data.size() < headerSize + baseSize) {
        error = "CMP file truncated in base grid";
        return false;
    }

    size_t offset = headerSize;
    map.base.resize(256 * 256);
    for (size_t i = 0; i < map.base.size(); ++i) {
        map.base[i] = rd32(data.data() + offset + i * 4);
        if ((map.base[i] & 1u) != 0) {
            error = "CMP base grid contains an odd offset";
            return false;
        }
    }
    offset += baseSize;

    if (offset + map.columnSize + map.blockSize > data.size()) {
        error = "CMP file truncated in column/block data";
        return false;
    }

    map.column.resize(map.columnSize / 2);
    for (size_t i = 0; i < map.column.size(); ++i) {
        map.column[i] = rd16(data.data() + offset + i * 2);
    }
    offset += map.columnSize;

    map.blocks.resize(map.blockSize / 8);
    for (size_t i = 0; i < map.blocks.size(); ++i) {
        const uint8_t* p = data.data() + offset + i * 8;
        map.blocks[i].typeMap = rd16(p);
        map.blocks[i].typeMapExt = p[2];
        map.blocks[i].left = p[3];
        map.blocks[i].right = p[4];
        map.blocks[i].top = p[5];
        map.blocks[i].bottom = p[6];
        map.blocks[i].lid = p[7];
    }
    offset += map.blockSize;

    if (offset + map.objectPosSize > data.size()) {
        error = "CMP file truncated in object data";
        return false;
    }
    const size_t objectCount = map.objectPosSize / 16;
    map.objects.resize(objectCount);
    for (size_t i = 0; i < objectCount; ++i) {
        const uint8_t* p = data.data() + offset + i * 16;
        MapObject& o = map.objects[i];
        o.x = rd16(p + 0);
        o.y = rd16(p + 2);
        o.z = rd16(p + 4);
        o.type = p[6];
        o.remap = p[7];
        o.rotation = rd16(p + 8);
        o.pitch = rd16(p + 10);
        o.roll = rd16(p + 12);
    }

    // Route, locations (36 * 3 bytes) and the named-sector navigation table.
    offset += map.objectPosSize;
    offset += map.routeSize;
    offset += 36 * 3;
    if (map.navDataSize >= 35 && offset + map.navDataSize <= data.size()) {
        const size_t sectorCount = map.navDataSize / 35;
        map.sectors.reserve(sectorCount);
        for (size_t i = 0; i < sectorCount; ++i) {
            const uint8_t* p = data.data() + offset + i * 35;
            MapSector sector;
            sector.x = p[0];
            sector.y = p[1];
            sector.w = p[2];
            sector.h = p[3];
            sector.sample = p[4];
            if (sector.w == 0 || sector.h == 0) continue;
            map.sectors.push_back(std::move(sector));
        }
    }

    out = std::move(map);
    return true;
}

void Palette::apply(const uint8_t* indices, int count, uint8_t* rgba,
                    bool transparentZero) const {
    for (int i = 0; i < count; ++i) {
        const uint8_t idx = indices[i];
        rgba[i * 4 + 0] = rgb[idx * 3 + 0];
        rgba[i * 4 + 1] = rgb[idx * 3 + 1];
        rgba[i * 4 + 2] = rgb[idx * 3 + 2];
        rgba[i * 4 + 3] = (transparentZero && idx == 0) ? 0 : 255;
    }
}

namespace {

// Parses the variable-sized car table.
bool parseCars(const std::vector<uint8_t>& data, size_t offset, size_t size,
               std::vector<GryCarInfo>& cars, std::string& error) {
    const size_t end = offset + size;
    if (end > data.size()) {
        error = "GRY car info out of bounds";
        return false;
    }
    size_t p = offset;
    while (p + 174 <= end) {
        GryCarInfo car;
        const uint8_t* b = data.data() + p;
        car.width = rds16(b + 0);
        car.height = rds16(b + 2);
        car.depth = rds16(b + 4);
        car.sprNum = rds16(b + 6);
        car.weightDescriptor = rds16(b + 8);
        car.maxSpeed = rds16(b + 10);
        car.minSpeed = rds16(b + 12);
        car.acceleration = rds16(b + 14);
        car.braking = rds16(b + 16);
        car.grip = rds16(b + 18);
        car.handling = rds16(b + 20);
        // 22..93: 12 * 3 int16 HLS remap (unused)
        std::memcpy(car.remap8, b + 94, 12);
        car.vtype = b[106];
        car.model = b[107];
        car.turning = b[108];
        car.damagable = b[109];
        // values, cx/cy, moment, fixed physics, doors
        const int16_t numDoors = rds16(b + 172);
        const size_t recordSize = 174 + static_cast<size_t>(numDoors < 0 ? 0 : numDoors) * 8;
        if (p + recordSize > end) break;
        p += recordSize;
        cars.push_back(car);
    }
    return true;
}

} // namespace

bool loadGry(const std::string& path, GryFile& out, std::string& error) {
    std::vector<uint8_t> data;
    if (!readFile(path, data, error)) return false;
    if (data.size() < 52) {
        error = "GRY file is too small";
        return false;
    }

    GryFile gry;
    // The file is little-endian; on big-endian hosts byteswap would be needed.
    gry.version = rd32(data.data() + 0);
    gry.sideSize = rd32(data.data() + 4);
    gry.lidSize = rd32(data.data() + 8);
    gry.auxSize = rd32(data.data() + 12);
    gry.animSize = rd32(data.data() + 16);
    gry.paletteSize = rd32(data.data() + 20);
    gry.remapSize = rd32(data.data() + 24);
    gry.remapIndexSize = rd32(data.data() + 28);
    gry.objectInfoSize = rd32(data.data() + 32);
    gry.carInfoSize = rd32(data.data() + 36);
    gry.spriteInfoSize = rd32(data.data() + 40);
    gry.spriteGraphicsSize = rd32(data.data() + 44);
    gry.spriteNumberSize = rd32(data.data() + 48);

    if (gry.version != 325) {
        error = "unsupported GRY version (expected 325, got " +
                std::to_string(gry.version) + ")";
        return false;
    }
    if (gry.sideSize % 4096 != 0 || gry.lidSize % 4096 != 0 ||
        gry.auxSize % 4096 != 0 || gry.spriteNumberSize != 42) {
        error = "GRY header has unexpected block sizes";
        return false;
    }
    if (gry.paletteSize != 768) {
        error = "GRY palette is not 768 bytes";
        return false;
    }

    size_t p = 52;
    auto take = [&](size_t n, std::vector<uint8_t>& dst) -> bool {
        if (p + n > data.size()) return false;
        dst.assign(data.begin() + static_cast<std::ptrdiff_t>(p),
                   data.begin() + static_cast<std::ptrdiff_t>(p + n));
        p += n;
        return true;
    };

    const size_t tileBytes = static_cast<size_t>(gry.sideSize) + gry.lidSize +
                             gry.auxSize;
    if (!take(tileBytes, gry.rawTiles)) {
        error = "GRY truncated in tile data";
        return false;
    }

    const int totalTiles = gry.numSide() + gry.numLid() + gry.numAux();
    const size_t trail = static_cast<size_t>((4 - (totalTiles % 4)) % 4) * 4096;
    // Pad the tile storage to a whole number of four-tile groups so the
    // interleaved decoder can always address complete groups.
    gry.rawTiles.resize(static_cast<size_t>((totalTiles + 3) / 4) * 4 * 4096, 0);
    p += trail;
    p += gry.animSize;
    if (p > data.size()) {
        error = "GRY truncated in animation data";
        return false;
    }

    if (gry.paletteSize != 768 || p + 768 > data.size()) {
        error = "GRY truncated in palette";
        return false;
    }
    for (int i = 0; i < 768; ++i) {
        const uint8_t v = data[p + i];
        gry.palette.rgb[i] = static_cast<uint8_t>((v << 2) | (v >> 4));
    }
    p += 768;

    if (p + gry.remapSize > data.size()) {
        error = "GRY truncated in remap tables";
        return false;
    }
    gry.remapTables.assign(data.begin() + static_cast<std::ptrdiff_t>(p),
                           data.begin() + static_cast<std::ptrdiff_t>(p + gry.remapSize));
    p += gry.remapSize;
    p += gry.remapIndexSize; // not needed for rendering

    if (p + gry.objectInfoSize > data.size()) {
        error = "GRY truncated in object info";
        return false;
    }
    const size_t objectCount = gry.objectInfoSize / 20;
    gry.objectsInfo.resize(objectCount);
    for (size_t i = 0; i < objectCount; ++i) {
        const uint8_t* b = data.data() + p + i * 20;
        GryObjectInfo& info = gry.objectsInfo[i];
        info.width = rd32(b + 0);
        info.height = rd32(b + 4);
        info.depth = rd32(b + 8);
        info.sprNum = rd16(b + 12);
        info.weight = rd16(b + 14);
        info.aux = rd16(b + 16);
        info.status = static_cast<int8_t>(b[18]);
        info.numInto = b[19];
    }
    p += gry.objectInfoSize;

    if (!parseCars(data, p, gry.carInfoSize, gry.cars, error)) return false;
    p += gry.carInfoSize;

    // Sprite info table
    const size_t spriteEnd = p + gry.spriteInfoSize;
    if (spriteEnd > data.size()) {
        error = "GRY truncated in sprite info";
        return false;
    }
    while (p + 10 <= spriteEnd) {
        GrySpriteInfo s;
        const uint8_t* b = data.data() + p;
        s.w = b[0];
        s.h = b[1];
        s.deltaCount = b[2];
        s.flags = b[3];
        s.size = rd16(b + 4);
        const uint32_t ptr = rd32(b + 6);
        s.page = static_cast<uint16_t>(ptr / 65536);
        s.xoffset = static_cast<uint8_t>((ptr % 65536) % 256);
        s.yoffset = static_cast<uint8_t>((ptr % 65536) / 256);
        p += 10;
        if (s.deltaCount > 32) {
            error = "GRY sprite has too many deltas";
            return false;
        }
        for (uint8_t i = 0; i < s.deltaCount; ++i) {
            if (p + 6 > spriteEnd) {
                error = "GRY sprite delta table truncated";
                return false;
            }
            GrySpriteInfo::Delta d;
            d.size = rd16(data.data() + p);
            d.ptr = rd32(data.data() + p + 2);
            p += 6;
            s.deltas.push_back(d);
        }
        gry.sprites.push_back(std::move(s));
    }
    p = spriteEnd;

    if (p + gry.spriteGraphicsSize > data.size()) {
        error = "GRY truncated in sprite graphics";
        return false;
    }
    gry.spriteGraphics.assign(data.begin() + static_cast<std::ptrdiff_t>(p),
                              data.begin() + static_cast<std::ptrdiff_t>(p + gry.spriteGraphicsSize));
    p += gry.spriteGraphicsSize;

    if (p + 42 > data.size()) {
        error = "GRY truncated in sprite numbers";
        return false;
    }
    for (int i = 0; i < 21; ++i) gry.spriteNumbers[i] = rd16(data.data() + p + i * 2);

    out = std::move(gry);
    return true;
}

bool GryFile::tileData(int slot, uint8_t* dst) const {
    if (!dst || slot < 0 || slot >= totalSlots()) return false;
    const size_t group = static_cast<size_t>(slot / 4) * 4 * 4096;
    const size_t column = static_cast<size_t>(slot % 4) * 64;
    if (group + 4 * 4096 > rawTiles.size()) return false;
    const uint8_t* src = rawTiles.data() + group + column;
    for (int y = 0; y < 64; ++y) {
        std::memcpy(dst + y * 64, src + static_cast<size_t>(y) * 256, 64);
    }
    return true;
}

bool GryFile::spriteRGBA(int index, std::vector<uint8_t>& rgba, int& w, int& h) const {
    if (index < 0 || index >= static_cast<int>(sprites.size())) return false;
    const GrySpriteInfo& s = sprites[index];
    if (s.w == 0 || s.h == 0) return false;

    const size_t pageSize = 256u * 256u;
    const size_t base = static_cast<size_t>(s.page) * pageSize;
    if (base + pageSize > spriteGraphics.size()) return false;

    w = s.w;
    h = s.h;
    rgba.assign(static_cast<size_t>(w) * h * 4, 0);
    for (int y = 0; y < h; ++y) {
        const int sy = s.yoffset + y;
        if (sy < 0 || sy >= 256) continue;
        for (int x = 0; x < w; ++x) {
            const int sx = s.xoffset + x;
            if (sx < 0 || sx >= 256) continue;
            const uint8_t idx = spriteGraphics[base + static_cast<size_t>(sy) * 256 + sx];
            const size_t o = (static_cast<size_t>(y) * w + x) * 4;
            rgba[o + 0] = palette.rgb[idx * 3 + 0];
            rgba[o + 1] = palette.rgb[idx * 3 + 1];
            rgba[o + 2] = palette.rgb[idx * 3 + 2];
            rgba[o + 3] = idx == 0 ? 0 : 255;
        }
    }
    return true;
}

} // namespace gta
