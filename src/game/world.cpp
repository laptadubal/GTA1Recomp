#include "game/world.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace gta {

namespace {

// Original GTA1 cube/slope models (45 types).  Face order inside each model:
//   0 = lid, 1 = z=1 side, 2 = z=0 side, 3 = x=0 side, 4 = x=1 side.
// Vertices are local to one cube: x/z in [0,1], y in [0,1].
const float kSlopeRaw[45][5][4][3] = {
#include "slope_raw.inc"
};

// Per-face texture coordinates used by the original renderer:
//   0 = z=1 side, 1 = z=0 side, 2 = x=0 side, 3 = x=1 side.
const float kSlopeTex[45][4][4][2] = {
#include "slope_tex.inc"
};

} // namespace

World::~World() { destroy(); }

void World::destroy() {
    for (CityChunk& c : chunks_) {
        if (c.vao) glDeleteVertexArrays(1, &c.vao);
        if (c.vbo) glDeleteBuffers(1, &c.vbo);
        if (c.ibo) glDeleteBuffers(1, &c.ibo);
    }
    chunks_.clear();
}

int World::visualHeight(int surface) const {
    if (surface <= 0) return baseHeight_;
    return surface <= static_cast<int>(baseHeight_) + 1
               ? static_cast<int>(baseHeight_)
               : surface;
}

int World::surfaceHeight(int cellX, int cellY) const {
    if (cellX < 0 || cellY < 0 || cellX >= CityMap::kSize || cellY >= CityMap::kSize)
        return baseHeight_;
    return visualHeight(surface_[cellY * CityMap::kSize + cellX]);
}

int World::roadSurface(int cellX, int cellY) const {
    if (!assets_) return 0;
    if (cellX < 0 || cellY < 0 || cellX >= CityMap::kSize || cellY >= CityMap::kSize)
        return 0;

    // Find the highest *opaque* lid.  GTA1 stores trees, road markings and
    // other decals as extra "flat" blocks stacked on top of the real surface;
    // they must not hide the road underneath them.  Structural blocks without
    // a lid are ignored as well.
    const CityMap& map = assets_->map();
    const int count = map.blockCount(cellX, cellY);
    for (int z = count - 1; z >= 0; --z) {
        const MapBlock* block = map.blockAt(cellX, cellY, z);
        if (!block || block->isAir()) continue;
        if (block->isFlat() || block->lid == 0) continue;
        // Roofs can be asphalt-coloured; only ground-level lids are road.
        if (z + 1 > static_cast<int>(baseHeight_) + 1) return 0;
        if (!assets_->isRoadTile(block->lid)) return 0;
        return visualHeight(z + 1);
    }
    return 0;
}

bool World::isPassable(float worldX, float worldZ) const {
    if (!assets_) return false;
    const int cx = static_cast<int>(std::floor(worldX));
    const int cy = static_cast<int>(std::floor(worldZ));
    if (cx < 0 || cy < 0 || cx >= CityMap::kSize || cy >= CityMap::kSize)
        return false;
    const CityMap& map = assets_->map();
    const int n = map.blockCount(cx, cy);
    if (n <= 0) return false;
    const int surface = surface_[cy * CityMap::kSize + cx];
    if (surface <= 0) {
        // A column with only decals/floors is walkable unless its floor was
        // classified as water (park lakes, canals).
        return floorWater_[static_cast<size_t>(cy * CityMap::kSize + cx)] == 0;
    }
    // Buildings block the car; water does not carry it.
    if (visualHeight(surface) > static_cast<int>(baseHeight_)) return false;
    for (int z = n - 1; z >= 0; --z) {
        const MapBlock* block = map.blockAt(cx, cy, z);
        if (!block || block->isAir() || block->isFlat() || block->lid == 0)
            continue;
        return !assets_->isWaterTile(block->lid);
    }
    return true;
}

bool World::isDrivable(float worldX, float worldZ) const {
    const int cx = static_cast<int>(std::floor(worldX));
    const int cy = static_cast<int>(std::floor(worldZ));
    return roadSurface(cx, cy) > 0;
}

Vec3 World::findSpawn(int preferredX, int preferredY) const {
    // Start on a straight street rather than in a wide plaza or car park:
    // the first pass wants exactly two opposite road neighbours, the second a
    // junction, the last any drivable cell near the preferred point.
    for (int pass = 0; pass < 3; ++pass) {
        for (int radius = 0; radius < 160; ++radius) {
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != radius) continue;
                    const int x = preferredX + dx;
                    const int y = preferredY + dy;
                    if (x < 1 || y < 1 || x >= CityMap::kSize - 1 ||
                        y >= CityMap::kSize - 1)
                        continue;
                    const int surface = roadSurface(x, y);
                    if (surface <= 0) continue;
                    if (pass == 0) {
                        // A proper street: narrow across, long along.  Using
                        // the corridor extents (not just the four immediate
                        // neighbours) keeps the spawn out of wide junctions
                        // and parking plazas, where the interior cells touch
                        // road on all four sides.
                        int east = 0, west = 0, south = 0, north = 0;
                        while (east < 12 && roadSurface(x + east + 1, y) > 0)
                            ++east;
                        while (west < 12 && roadSurface(x - west - 1, y) > 0)
                            ++west;
                        while (south < 12 && roadSurface(x, y + south + 1) > 0)
                            ++south;
                        while (north < 12 && roadSurface(x, y - north - 1) > 0)
                            ++north;
                        const int widthX = west + east + 1;
                        const int widthZ = north + south + 1;
                        const bool vertical = widthZ >= 4 && widthX <= 5;
                        const bool horizontal = widthX >= 4 && widthZ <= 5;
                        if (!vertical && !horizontal) continue;
                    } else if (pass == 1) {
                        const int roads =
                            (roadSurface(x, y - 1) > 0 ? 1 : 0) +
                            (roadSurface(x, y + 1) > 0 ? 1 : 0) +
                            (roadSurface(x - 1, y) > 0 ? 1 : 0) +
                            (roadSurface(x + 1, y) > 0 ? 1 : 0);
                        if (roads < 2) continue;
                    }
                    return Vec3{x + 0.5f, static_cast<float>(surface), y + 0.5f};
                }
            }
        }
    }
    return Vec3{128.5f, 1.0f, 128.5f};
}

void World::addQuad(std::vector<CityVertex>& v, std::vector<uint32_t>& idx,
                    const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3,
                    const TileUV& uv, float r, float g, float b, bool transparent,
                    bool faceUp) {
    (void)transparent;
    const uint32_t base = static_cast<uint32_t>(v.size());
    v.push_back({p0.x, p0.y, p0.z, uv.u0, uv.v1, r, g, b, 1.0f});
    v.push_back({p1.x, p1.y, p1.z, uv.u1, uv.v1, r, g, b, 1.0f});
    v.push_back({p2.x, p2.y, p2.z, uv.u1, uv.v0, r, g, b, 1.0f});
    v.push_back({p3.x, p3.y, p3.z, uv.u0, uv.v0, r, g, b, 1.0f});
    if (faceUp) {
        // Horizontal quads are authored clockwise seen from above; reverse
        // them so the normal points up and back-face culling keeps them.
        idx.push_back(base + 0);
        idx.push_back(base + 2);
        idx.push_back(base + 1);
        idx.push_back(base + 0);
        idx.push_back(base + 3);
        idx.push_back(base + 2);
    } else {
        idx.push_back(base + 0);
        idx.push_back(base + 1);
        idx.push_back(base + 2);
        idx.push_back(base + 0);
        idx.push_back(base + 2);
        idx.push_back(base + 3);
    }
}

bool World::build(AssetManager& assets, std::string& error) {
    destroy();
    assets_ = &assets;
    const CityMap& map = assets.map();
    if (!map.valid()) {
        error = "cannot build world from an invalid city map";
        return false;
    }

    // Height of the highest *drawn* (non-flat) lid in each cell.  Invisible
    // structural crowns and transparent decals must not raise the ground:
    // otherwise the terrain around them ends up at the wrong level and the
    // sea / park floors no longer meet the streets.
    const int cells = CityMap::kSize * CityMap::kSize;
    surface_.assign(static_cast<size_t>(cells), 0);
    baseLid_.assign(static_cast<size_t>(cells), 0);
    baseRot_.assign(static_cast<size_t>(cells), 0);
    int heightHistogram[16] = {0};
    for (int y = 0; y < CityMap::kSize; ++y) {
        for (int x = 0; x < CityMap::kSize; ++x) {
            const int n = map.blockCount(x, y);
            int topZ = 0;
            for (int z = n - 1; z >= 0; --z) {
                const MapBlock* b = map.blockAt(x, y, z);
                if (!b || b->isAir() || b->lid == 0 || b->isFlat()) continue;
                topZ = z + 1;
                const size_t index = static_cast<size_t>(y * CityMap::kSize + x);
                baseLid_[index] = b->lid;
                baseRot_[index] = b->rotation();
                break;
            }
            surface_[y * CityMap::kSize + x] =
                static_cast<uint8_t>(std::min(topZ, 255));
            if (topZ >= 0 && topZ < 16) ++heightHistogram[topZ];
        }
    }
    int baseHeight = 1;
    for (int h = 1; h < 16; ++h) {
        if (heightHistogram[h] > heightHistogram[baseHeight]) baseHeight = h;
    }
    baseHeight_ = static_cast<uint8_t>(baseHeight);

    // Columns that carry only transparent decals (a fence/sign row with no
    // ground block of its own) take the ground texture of the four directly
    // adjacent cells.  The original maps always place such columns next to
    // authored sidewalks/roads, so one hop is all that is ever needed.
    // Nothing further is propagated: open water and empty gaps are left to
    // the explicit water/grass floors below.
    for (int y = 0; y < CityMap::kSize; ++y) {
        for (int x = 0; x < CityMap::kSize; ++x) {
            const size_t index = static_cast<size_t>(y * CityMap::kSize + x);
            if (surface_[index] != 0) continue;
            if (map.blockCount(x, y) <= 0) continue;
            int counts[256] = {0};
            int bestLid = 0;
            int bestCount = 0;
            uint8_t bestRot = 0;
            const int nx[4] = {x - 1, x + 1, x, x};
            const int ny[4] = {y, y, y - 1, y + 1};
            for (int d = 0; d < 4; ++d) {
                if (nx[d] < 0 || ny[d] < 0 || nx[d] >= CityMap::kSize ||
                    ny[d] >= CityMap::kSize)
                    continue;
                const size_t ni =
                    static_cast<size_t>(ny[d] * CityMap::kSize + nx[d]);
                const int lid = baseLid_[ni];
                if (lid == 0) continue;
                const int count = ++counts[lid];
                if (count > bestCount) {
                    bestCount = count;
                    bestLid = lid;
                    bestRot = baseRot_[ni];
                }
            }
            baseLid_[index] = static_cast<uint8_t>(bestLid);
            baseRot_[index] = bestRot;
        }
    }

    // Classify every cell as land or water.  Cells without a visible top are
    // then assigned to the surrounding body: open sea connected to the map
    // border becomes water, enclosed park gaps become grass, and gaps next
    // to real water tiles become water as well.
    floorWater_.assign(static_cast<size_t>(cells), 0);
    std::vector<uint8_t> known(static_cast<size_t>(cells), 0); // 1 land, 2 water
    for (int y = 0; y < CityMap::kSize; ++y) {
        for (int x = 0; x < CityMap::kSize; ++x) {
            const int n = map.blockCount(x, y);
            if (n <= 0) continue;
            bool water = false;
            for (int z = n - 1; z >= 0; --z) {
                const MapBlock* b = map.blockAt(x, y, z);
                if (!b || b->isAir() || b->lid == 0) continue;
                if (!b->isFlat() && assets.isWaterTile(b->lid)) water = true;
                break;
            }
            known[static_cast<size_t>(y * CityMap::kSize + x)] = water ? 2 : 1;
        }
    }

    // Mark empty regions that reach the map border as sea.
    std::vector<uint8_t> visited(static_cast<size_t>(cells), 0);
    std::vector<int> stack;
    auto isOpen = [&](int x, int y) {
        const size_t i = static_cast<size_t>(y * CityMap::kSize + x);
        return surface_[i] == 0 && known[i] == 0 && !visited[i];
    };
    for (int x = 0; x < CityMap::kSize; ++x) {
        for (int y : {0, CityMap::kSize - 1}) {
            if (isOpen(x, y)) {
                visited[static_cast<size_t>(y * CityMap::kSize + x)] = 1;
                stack.push_back(y * CityMap::kSize + x);
            }
        }
    }
    for (int y = 0; y < CityMap::kSize; ++y) {
        for (int x : {0, CityMap::kSize - 1}) {
            if (isOpen(x, y)) {
                visited[static_cast<size_t>(y * CityMap::kSize + x)] = 1;
                stack.push_back(y * CityMap::kSize + x);
            }
        }
    }
    while (!stack.empty()) {
        const int index = stack.back();
        stack.pop_back();
        floorWater_[static_cast<size_t>(index)] = 1;
        const int x = index % CityMap::kSize;
        const int y = index / CityMap::kSize;
        const int nx[4] = {x - 1, x + 1, x, x};
        const int ny[4] = {y, y, y - 1, y + 1};
        for (int i = 0; i < 4; ++i) {
            if (nx[i] < 0 || ny[i] < 0 || nx[i] >= CityMap::kSize ||
                ny[i] >= CityMap::kSize)
                continue;
            if (!isOpen(nx[i], ny[i])) continue;
            visited[static_cast<size_t>(ny[i] * CityMap::kSize + nx[i])] = 1;
            stack.push_back(ny[i] * CityMap::kSize + nx[i]);
        }
    }

    // Enclosed gaps: water if a water tile is close, grass otherwise.
    for (int y = 0; y < CityMap::kSize; ++y) {
        for (int x = 0; x < CityMap::kSize; ++x) {
            const size_t i = static_cast<size_t>(y * CityMap::kSize + x);
            if (surface_[i] != 0 || known[i] != 0) continue;
            bool water = floorWater_[i] != 0;
            for (int radius = 1; radius <= 3 && !water; ++radius) {
                for (int dy = -radius; dy <= radius && !water; ++dy) {
                    for (int dx = -radius; dx <= radius; ++dx) {
                        if (std::max(std::abs(dx), std::abs(dy)) != radius)
                            continue;
                        const int tx = x + dx;
                        const int ty = y + dy;
                        if (tx < 0 || ty < 0 || tx >= CityMap::kSize ||
                            ty >= CityMap::kSize)
                            continue;
                        if (known[static_cast<size_t>(ty * CityMap::kSize + tx)] == 2) {
                            water = true;
                            break;
                        }
                    }
                }
            }
            floorWater_[i] = water ? 1 : 0;
        }
    }

    chunks_.resize(kChunks * kChunks);
    for (int cy = 0; cy < kChunks; ++cy) {
        for (int cx = 0; cx < kChunks; ++cx) {
            std::vector<CityVertex> vertices;
            std::vector<uint32_t> indices;
            GLsizei opaqueIndices = 0;
            if (!buildChunk(cx, cy, vertices, indices, opaqueIndices)) continue;
            if (indices.empty()) continue;

            CityChunk& chunk = chunks_[cy * kChunks + cx];
            chunk.cx = cx;
            chunk.cy = cy;
            chunk.indexCount = static_cast<GLsizei>(indices.size());
            chunk.opaqueIndexCount = opaqueIndices;
            chunk.minX = static_cast<float>(cx * kChunkSize);
            chunk.minZ = static_cast<float>(cy * kChunkSize);
            chunk.maxX = chunk.minX + kChunkSize;
            chunk.maxZ = chunk.minZ + kChunkSize;

            glGenVertexArrays(1, &chunk.vao);
            glGenBuffers(1, &chunk.vbo);
            glGenBuffers(1, &chunk.ibo);

            glBindVertexArray(chunk.vao);
            glBindBuffer(GL_ARRAY_BUFFER, chunk.vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(vertices.size() * sizeof(CityVertex)),
                         vertices.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk.ibo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                         indices.data(), GL_STATIC_DRAW);

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
        }
    }
    return true;
}

bool World::buildChunk(int cx, int cy, std::vector<CityVertex>& vertices,
                       std::vector<uint32_t>& indices, GLsizei& opaqueIndices) {
    if (!assets_) return false;
    const CityMap& map = assets_->map();
    const AssetManager& assets = *assets_;

    // Diagnostic filters (audit): GTA_DBG=nosides|nolids|nofloor isolates the
    // geometry classes so the screenshot shows what the CMP actually contains.
    std::string dbgFilter;
    if (const char* e = std::getenv("GTA_DBG")) dbgFilter = e;
    const bool dbgNoSides = dbgFilter == "nosides";
    const bool dbgNoLids = dbgFilter == "nolids";
    const bool dbgNoFloor = dbgFilter == "nofloor";
    const bool dbgIdColor = dbgFilter == "idcolor";
    const bool dbgCellColor = dbgFilter == "cellcolor";

    std::vector<uint32_t> opaqueIndex;
    std::vector<uint32_t> alphaIndex;

    const TileUV waterFloor = assets.lidUV(assets.waterFallbackLid());
    const TileUV grassFloor = assets.lidUV(assets.grassFallbackLid());

    struct FaceDef {
        int rawFace;
        int texSet;
        int fieldIndex;
        int nx;
        int ny;
        bool mirrorTopBottom;
    };
    // CMP side fields are stored in a 90-degree-rotated order compared with
    // the cube's geometric sides.  Verified against every exposed face of all
    // three city maps (NYC/MIAMI/SANB): the field that matches a north face is
    // "left", south is "right", west is "top" and east is "bottom".
    const FaceDef faces[4] = {
        {2, 1, 2, 0, -1, true},   // north (z = 0)
        {1, 0, 3, 0, 1, true},    // south (z = 1)
        {3, 2, 4, -1, 0, false},  // west  (x = 0)
        {4, 3, 5, 1, 0, false},   // east  (x = 1)
    };
    // The 45-degree models leave one vertical face degenerate.
    const int skipSlopeFace[4] = {42, 41, 44, 43};

    auto fieldOf = [](const MapBlock& b, int index) -> uint8_t {
        switch (index) {
        case 2: return b.left;
        case 3: return b.right;
        case 4: return b.top;
        default: return b.bottom;
        }
    };

    auto addFace = [&](std::vector<uint32_t>& target, const float pts[4][3],
                       const float uu[4], const float vv[4], float cr, float cg,
                       float cb, const Vec3& inside) {
        const Vec3 p0{pts[0][0], pts[0][1], pts[0][2]};
        const Vec3 p1{pts[1][0], pts[1][1], pts[1][2]};
        const Vec3 p2{pts[2][0], pts[2][1], pts[2][2]};
        const Vec3 p3{pts[3][0], pts[3][1], pts[3][2]};
        const Vec3 normal = (p1 - p0).cross(p2 - p0);
        const Vec3 centre = (p0 + p1 + p2 + p3) * 0.25f;
        const bool reverse = normal.dot(centre - inside) < 0.0f;
        const Vec3* points[4] = {&p0, &p1, &p2, &p3};
        const uint32_t base = static_cast<uint32_t>(vertices.size());
        for (int i = 0; i < 4; ++i) {
            vertices.push_back({points[i]->x, points[i]->y, points[i]->z,
                                uu[i], vv[i], cr, cg, cb, 1.0f});
        }
        if (reverse) {
            target.push_back(base + 0);
            target.push_back(base + 2);
            target.push_back(base + 1);
            target.push_back(base + 0);
            target.push_back(base + 3);
            target.push_back(base + 2);
        } else {
            target.push_back(base + 0);
            target.push_back(base + 1);
            target.push_back(base + 2);
            target.push_back(base + 0);
            target.push_back(base + 2);
            target.push_back(base + 3);
        }
    };

    auto emitLidUV = [&](int x, int y, int base, const TileUV& uvIn,
                         int rotation, int slope, bool transparent,
                         float cr = 1.0f, float cg = 1.0f, float cb = 1.0f) {
        TileUV uv = uvIn;
        if (uv.u1 <= uv.u0 || uv.v1 <= uv.v0) return;
        if (slope < 0 || slope > 44) slope = 0;
        rotation &= 3;
        float pts[4][3];
        float uu[4];
        float vv[4];
        for (int j = 0; j < 4; ++j) {
            pts[j][0] = static_cast<float>(x) + kSlopeRaw[slope][0][j][0];
            pts[j][1] = static_cast<float>(base) + kSlopeRaw[slope][0][j][1];
            pts[j][2] = static_cast<float>(y) + kSlopeRaw[slope][0][j][2];
            const int k = (4 - rotation - j) & 3;
            uu[j] = (k == 1 || k == 2) ? uv.u1 : uv.u0;
            vv[j] = (k >= 2) ? uv.v1 : uv.v0;
        }
        const Vec3 inside{static_cast<float>(x) + 0.5f,
                          static_cast<float>(base) + 0.5f,
                          static_cast<float>(y) + 0.5f};
        addFace(transparent ? alphaIndex : opaqueIndex, pts, uu, vv, cr, cg,
                cb, inside);
    };

    auto emitLid = [&](int x, int y, int base, uint8_t lid, int remap,
                       int rotation, int slope, bool transparent) {
        if (!lid) return;
        if (dbgNoLids) return;
        // Tiles with transparency-key pixels keep the original colour-key
        // behaviour: they render in the alpha-test pass so the holes show
        // whatever is behind instead of coming out solid black.
        if (assets.lidHasAlpha(lid)) transparent = true;
        TileUV uv = assets.lidUV(lid, remap);
        float cr = 1.0f, cg = 1.0f, cb = 1.0f;
        if (dbgIdColor) {
            uv = assets.whiteUV();
            cr = static_cast<float>((lid * 37) % 256) / 255.0f;
            cg = static_cast<float>((lid * 101) % 256) / 255.0f;
            cb = static_cast<float>((lid * 151) % 256) / 255.0f;
        }
        if (dbgCellColor) {
            uv = assets.whiteUV();
            cr = static_cast<float>(x) / 255.0f;
            cg = static_cast<float>(y) / 255.0f;
            cb = 0.2f;
        }
        emitLidUV(x, y, base, uv, rotation, slope, transparent, cr, cg, cb);
    };

    // A vertical face on one of the four cell edges.  The original engine
    // stores facade textures with the block that owns that height, so a
    // multi-storey wall simply repeats the authored tile per level.
    auto emitSide = [&](int x, int y, int base, int face, uint8_t tile,
                        int remap, bool mirror, bool transparent) {
        if (!tile) return;
        if (dbgNoSides) return;
        TileUV uv = assets.sideUV(tile, remap);
        if (uv.u1 <= uv.u0 || uv.v1 <= uv.v0) return;
        float cr = 1.0f, cg = 1.0f, cb = 1.0f;
        if (dbgIdColor) {
            uv = assets.whiteUV();
            cr = static_cast<float>((tile * 37) % 256) / 255.0f;
            cg = static_cast<float>((tile * 101) % 256) / 255.0f;
            cb = static_cast<float>((tile * 151) % 256) / 255.0f;
        }
        if (dbgCellColor) {
            uv = assets.whiteUV();
            cr = static_cast<float>(x) / 255.0f;
            cg = static_cast<float>(y) / 255.0f;
            cb = 0.8f;
        }
        const MapBlock* block = map.blockAt(x, y, base);
        int slope = block ? block->slopeType() : 0;
        if (slope < 0 || slope > 44) slope = 0;
        if (slope == skipSlopeFace[face]) return;
        float pts[4][3];
        float uu[4];
        float vv[4];
        for (int j = 0; j < 4; ++j) {
            pts[j][0] = static_cast<float>(x) +
                        kSlopeRaw[slope][faces[face].rawFace][j][0];
            pts[j][1] = static_cast<float>(base) +
                        kSlopeRaw[slope][faces[face].rawFace][j][1];
            pts[j][2] = static_cast<float>(y) +
                        kSlopeRaw[slope][faces[face].rawFace][j][2];
            float tu = kSlopeTex[slope][faces[face].texSet][j][0];
            const float tv = kSlopeTex[slope][faces[face].texSet][j][1];
            if (mirror) tu = 1.0f - tu;
            uu[j] = uv.u0 + tu * (uv.u1 - uv.u0);
            vv[j] = uv.v0 + tv * (uv.v1 - uv.v0);
        }
        const Vec3 inside{static_cast<float>(x) + 0.5f,
                          static_cast<float>(base) + 0.5f,
                          static_cast<float>(y) + 0.5f};
        addFace(transparent ? alphaIndex : opaqueIndex, pts, uu, vv, cr, cg,
                cb, inside);
    };

    // Highest visible (non-transparent) block of a column.  Used only to skip
    // faces that are completely buried behind a neighbour.
    auto opaqueTop = [&](int x, int y) -> int {
        if (x < 0 || y < 0 || x >= CityMap::kSize || y >= CityMap::kSize)
            return 0;
        const int n = map.blockCount(x, y);
        for (int z = n - 1; z >= 0; --z) {
            const MapBlock* b = map.blockAt(x, y, z);
            if (!b || b->isAir() || b->isFlat()) continue;
            if (b->lid || b->left || b->right || b->top || b->bottom)
                return z + 1;
        }
        return 0;
    };

    // Procedural lane paint is an optional aid, never the reference path.
    auto roadAt = [&](int x, int y) { return roadSurface(x, y) > 0; };
    auto paintRect = [&](float ax, float az, float bx, float bz, float h) {
        float pts[4][3] = {
            {ax, h, az}, {bx, h, az}, {bx, h, bz}, {ax, h, bz}};
        const float uu[4] = {assets.whiteUV().u0, assets.whiteUV().u0,
                             assets.whiteUV().u0, assets.whiteUV().u0};
        const float vv[4] = {assets.whiteUV().v0, assets.whiteUV().v0,
                             assets.whiteUV().v0, assets.whiteUV().v0};
        const Vec3 inside{(ax + bx) * 0.5f, h - 0.5f, (az + bz) * 0.5f};
        addFace(opaqueIndex, pts, uu, vv, 0.98f, 0.80f, 0.12f, inside);
    };
    auto addRoadPaint = [&](int cx2, int cz, float lidY) {
        const float x0 = static_cast<float>(cx2);
        const float x1 = x0 + 1.0f;
        const float z0 = static_cast<float>(cz);
        const float z1 = z0 + 1.0f;
        const float y = lidY + 0.02f;
        const float inset = 0.055f;
        const float line = 0.05f;

        int east = 0, west = 0, south = 0, north = 0;
        while (east < 20 && roadAt(cx2 + east + 1, cz)) ++east;
        while (west < 20 && roadAt(cx2 - west - 1, cz)) ++west;
        while (south < 20 && roadAt(cx2, cz + south + 1)) ++south;
        while (north < 20 && roadAt(cx2, cz - north - 1)) ++north;
        const int widthX = west + east + 1;
        const int widthZ = north + south + 1;
        const bool vertical = widthZ >= 3 && widthX <= 8;
        const bool horizontal = widthX >= 3 && widthZ <= 8;
        if (!vertical && !horizontal) return;

        if (vertical) {
            if (west == 0)
                paintRect(x0 + inset, z0, x0 + inset + line, z1, y);
            if (east == 0)
                paintRect(x1 - inset - line, z0, x1 - inset, z1, y);
        }
        if (horizontal) {
            if (north == 0)
                paintRect(x0, z0 + inset, x1, z0 + inset + line, y);
            if (south == 0)
                paintRect(x0, z1 - inset - line, x1, z1 - inset, y);
        }
        auto paintAlong = [&](float position, bool alongZ) {
            if (alongZ) {
                if ((cz & 1) != 0) return;
                paintRect(position - line * 0.5f, z0 + 0.16f,
                          position + line * 0.5f, z1 - 0.16f, y);
            } else {
                if ((cx2 & 1) != 0) return;
                paintRect(x0 + 0.16f, position - line * 0.5f, x1 - 0.16f,
                          position + line * 0.5f, y);
            }
        };
        if (vertical) {
            const float low = static_cast<float>(cx2 - west);
            const float high = static_cast<float>(cx2 + east + 1);
            const float centre = (low + high) * 0.5f;
            for (int offset = 0; offset <= 8; offset += 2) {
                for (int side = -1; side <= 1; side += 2) {
                    if (offset == 0 && side == 1) continue;
                    const float position = centre + side * offset;
                    if (position < low + 0.5f || position > high - 0.5f)
                        continue;
                    if (std::floor(position) != static_cast<float>(cx2))
                        continue;
                    paintAlong(position, true);
                }
            }
        }
        if (horizontal) {
            const float low = static_cast<float>(cz - north);
            const float high = static_cast<float>(cz + south + 1);
            const float centre = (low + high) * 0.5f;
            for (int offset = 0; offset <= 8; offset += 2) {
                for (int side = -1; side <= 1; side += 2) {
                    if (offset == 0 && side == 1) continue;
                    const float position = centre + side * offset;
                    if (position < low + 0.5f || position > high - 0.5f)
                        continue;
                    if (std::floor(position) != static_cast<float>(cz))
                        continue;
                    paintAlong(position, false);
                }
            }
        }
    };

    for (int ly = 0; ly < kChunkSize; ++ly) {
        const int cellY = cy * kChunkSize + ly;
        for (int lx = 0; lx < kChunkSize; ++lx) {
            const int cellX = cx * kChunkSize + lx;
            const int n = map.blockCount(cellX, cellY);
            const size_t cellIndex =
                static_cast<size_t>(cellY * CityMap::kSize + cellX);
            const int top = opaqueTop(cellX, cellY);

            bool hasOpaqueLid = false;
            for (int z = 0; z < n; ++z) {
                const MapBlock* b = map.blockAt(cellX, cellY, z);
                if (b && !b->isAir() && !b->isFlat() && b->lid) {
                    hasOpaqueLid = true;
                    break;
                }
            }

            // Floor under decal-only columns, empty park gaps and open sea.
            if (!hasOpaqueLid && !dbgNoFloor) {
                const bool water =
                    (n <= 0) && floorWater_[cellIndex] != 0;
                int height = top > 0 ? top : static_cast<int>(baseHeight_);
                if (n <= 0 && water)
                    height = std::max(0, static_cast<int>(baseHeight_) - 1);
                TileUV floorUV;
                int floorRot = 0;
                if (baseLid_[cellIndex] != 0) {
                    floorUV = assets.lidUV(baseLid_[cellIndex]);
                    floorRot = baseRot_[cellIndex];
                } else if (n <= 0) {
                    floorUV = water ? waterFloor : grassFloor;
                }
                if (floorUV.u1 <= floorUV.u0) floorUV = grassFloor;
                if (floorUV.u1 > floorUV.u0) {
                    float fr = 1.0f, fg = 1.0f, fb = 1.0f;
                    if (dbgFilter == "floorcoord") {
                        floorUV = assets.whiteUV();
                        fr = static_cast<float>(cellX) / 255.0f;
                        fg = static_cast<float>(cellY) / 255.0f;
                        fb = 1.0f;
                    }
                    if (dbgCellColor) {
                        floorUV = assets.whiteUV();
                        fr = static_cast<float>(cellX) / 255.0f;
                        fg = static_cast<float>(cellY) / 255.0f;
                        fb = 0.5f;
                    }
                    emitLidUV(cellX, cellY, height - 1, floorUV, floorRot, 0,
                              false, fr, fg, fb);
                }
            }

            if (n <= 0) continue;

            for (int z = 0; z < n; ++z) {
                const MapBlock* block = map.blockAt(cellX, cellY, z);
                if (!block || block->isAir()) continue;
                const bool flat = block->isFlat();
                const int slope = std::min<int>(block->slopeType(), 44);

                if (block->lid) {
                    const bool plainRoad =
                        !flat && cleanRoads_ && z + 1 == top &&
                        top == static_cast<int>(baseHeight_) &&
                        assets.isRoadTile(block->lid) && !block->left &&
                        !block->right && !block->top && !block->bottom &&
                        !assets.isStructuralLid(block->lid);
                    if (plainRoad) {
                        emitLid(cellX, cellY, z,
                                static_cast<uint8_t>(assets.plainRoadLid()),
                                block->remapIndex(), block->rotation(), 0,
                                false);
                        addRoadPaint(cellX, cellY,
                                     static_cast<float>(z + 1));
                    } else if (flat) {
                        // Flat lids decorate the surface at their own base,
                        // not the top of the (transparent) block.  The CMP
                        // stores every decal one level above its surface.
                        emitLid(cellX, cellY, z - 1, block->lid,
                                block->remapIndex(), block->rotation(), slope,
                                true);
                    } else {
                        emitLid(cellX, cellY, z, block->lid,
                                block->remapIndex(), block->rotation(), slope,
                                false);
                    }
                }

                if (flat) {
                    // Fences/railings: the opposite field pair describes the
                    // two sides of one vertical plane.  Draw real edge
                    // geometry instead of a camera-facing billboard.
                    if (block->left || block->right) {
                        const uint8_t tile =
                            block->left ? block->left : block->right;
                        emitSide(cellX, cellY, z, 0, tile,
                                 block->remapIndex(), block->flipTopBottom(),
                                 true);
                    }
                    if (block->top || block->bottom) {
                        const uint8_t tile =
                            block->top ? block->top : block->bottom;
                        emitSide(cellX, cellY, z, 2, tile,
                                 block->remapIndex(), block->flipLeftRight(),
                                 true);
                    }
                } else {
                    for (int f = 0; f < 4; ++f) {
                        const int nx = cellX + faces[f].nx;
                        const int ny = cellY + faces[f].ny;
                        if (z < opaqueTop(nx, ny)) continue;
                        uint8_t tile = fieldOf(*block, faces[f].fieldIndex);
                        if (!tile) continue; // no authored face: leave open
                        const int remap = block->remapIndex();
                        const bool mirror = faces[f].mirrorTopBottom
                                                ? block->flipTopBottom()
                                                : block->flipLeftRight();
                        // Sign/fence-style facades contain transparency-key
                        // pixels and must keep the original colour-key
                        // behaviour (alpha-test pass, see emitLid above).
                        const bool keyed = assets.sideHasAlpha(tile);
                        emitSide(cellX, cellY, z, f, tile, remap, mirror,
                                 keyed);
                    }
                }
            }
        }
    }

    indices.clear();
    indices.reserve(opaqueIndex.size() + alphaIndex.size());
    indices.insert(indices.end(), opaqueIndex.begin(), opaqueIndex.end());
    indices.insert(indices.end(), alphaIndex.begin(), alphaIndex.end());
    opaqueIndices = static_cast<GLsizei>(opaqueIndex.size());
    return true;
}

} // namespace gta
