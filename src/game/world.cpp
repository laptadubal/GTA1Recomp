#include "game/world.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace gta {

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

    // Columns that only carry a transparent decal (fence, sign, road paint)
    // have no surface texture of their own.  Use the most common texture of
    // their immediate neighbours as the base; this keeps crosswalks on
    // asphalt and fences on grass instead of leaving dark holes or blue
    // squares in the middle of streets.
    for (int y = 0; y < CityMap::kSize; ++y) {
        for (int x = 0; x < CityMap::kSize; ++x) {
            const size_t index = static_cast<size_t>(y * CityMap::kSize + x);
            if (surface_[index] != 0) continue;
            if (map.blockCount(x, y) <= 0) continue;
            int counts[256] = {0};
            int bestLid = 0;
            int bestCount = 0;
            uint8_t bestRot = 0;
            for (int radius = 1; radius <= 3 && bestLid == 0; ++radius) {
                for (int dy = -radius; dy <= radius; ++dy) {
                    for (int dx = -radius; dx <= radius; ++dx) {
                        if (std::max(std::abs(dx), std::abs(dy)) != radius)
                            continue;
                        const int nx = x + dx;
                        const int ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= CityMap::kSize ||
                            ny >= CityMap::kSize)
                            continue;
                        const size_t ni =
                            static_cast<size_t>(ny * CityMap::kSize + nx);
                        const int lid = baseLid_[ni];
                        if (lid == 0) continue;
                        const int count = ++counts[lid];
                        if (count > bestCount) {
                            bestCount = count;
                            bestLid = lid;
                            bestRot = baseRot_[ni];
                        }
                    }
                }
            }
            baseLid_[index] = static_cast<uint8_t>(bestLid);
            baseRot_[index] = bestRot;
        }
    }

    // Remaining empty cells (open sea, park lakes) inherit the closest base
    // so the preview/floor code has a sensible texture for the coastline.
    {
        std::vector<uint8_t> visited(static_cast<size_t>(cells), 0);
        std::vector<int> queue;
        queue.reserve(static_cast<size_t>(cells));
        for (int i = 0; i < cells; ++i) {
            if (baseLid_[static_cast<size_t>(i)] != 0) {
                visited[static_cast<size_t>(i)] = 1;
                queue.push_back(i);
            }
        }
        for (size_t head = 0; head < queue.size(); ++head) {
            const int index = queue[head];
            const int x = index % CityMap::kSize;
            const int y = index / CityMap::kSize;
            const int nx[4] = {x - 1, x + 1, x, x};
            const int ny[4] = {y, y, y - 1, y + 1};
            for (int i = 0; i < 4; ++i) {
                if (nx[i] < 0 || ny[i] < 0 || nx[i] >= CityMap::kSize ||
                    ny[i] >= CityMap::kSize)
                    continue;
                const size_t next =
                    static_cast<size_t>(ny[i] * CityMap::kSize + nx[i]);
                if (visited[next]) continue;
                visited[next] = 1;
                baseLid_[next] = baseLid_[static_cast<size_t>(index)];
                baseRot_[next] = baseRot_[static_cast<size_t>(index)];
                queue.push_back(static_cast<int>(next));
            }
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

    std::vector<uint32_t> opaqueIndex;
    std::vector<uint32_t> alphaIndex;

    // Fallback tiles used for map blocks that do not carry their own texture.
    const TileUV fallbackWall = assets.sideUV(1);
    const TileUV fallbackRoad = assets.lidUV(assets.roadFallbackLid());
    const TileUV fallbackRoof = assets.lidUV(assets.roofFallbackLid());
    const TileUV waterFloor = assets.lidUV(assets.waterFallbackLid());
    const TileUV grassFloor = assets.lidUV(assets.grassFallbackLid());

    auto add = [&](const Vec3& p0, const Vec3& p1, const Vec3& p2,
                   const Vec3& p3, const TileUV& uv, float r, float g, float b,
                   bool transparent, bool faceUp = false) {
        addQuad(vertices, transparent ? alphaIndex : opaqueIndex, p0, p1, p2, p3,
                uv, r, g, b, transparent, faceUp);
    };

    // Horizontal map tiles are rotated in 90-degree steps by the two rotation
    // bits in typeMap.  Vertex order here is north-west, north-east,
    // south-east, south-west.  GTA1's stored rotation is measured against a
    // texture whose north edge points east, hence the extra +1 quarter turn;
    // without it lane markings and corner curbs end up at right angles to the
    // roads (the "asset salad" look).
    auto addLid = [&](const Vec3& p0, const Vec3& p1, const Vec3& p2,
                      const Vec3& p3, const TileUV& uv, int rotation,
                      bool transparent, uint8_t flips = 0) {
        std::vector<uint32_t>& index =
            transparent ? alphaIndex : opaqueIndex;
        TileUV fuv = uv;
        if (flips & 1) std::swap(fuv.v0, fuv.v1);
        if (flips & 2) std::swap(fuv.u0, fuv.u1);
        const float us[4] = {fuv.u0, fuv.u1, fuv.u1, fuv.u0};
        const float vs[4] = {fuv.v0, fuv.v0, fuv.v1, fuv.v1};
        const Vec3* points[4] = {&p0, &p1, &p2, &p3};
        const uint32_t base = static_cast<uint32_t>(vertices.size());
        for (int i = 0; i < 4; ++i) {
            const int corner = (i - rotation + 1) & 3;
            const Vec3& p = *points[i];
            vertices.push_back({p.x, p.y, p.z, us[corner], vs[corner], 1.0f,
                                1.0f, 1.0f, 1.0f});
        }
        // Horizontal face: reverse the winding so the normal points up for
        // back-face culling.
        index.push_back(base + 0);
        index.push_back(base + 2);
        index.push_back(base + 1);
        index.push_back(base + 0);
        index.push_back(base + 3);
        index.push_back(base + 2);
    };

    auto blockAtOrNull = [&](int x, int y, int z) -> const MapBlock* {
        if (x < 0 || y < 0 || x >= CityMap::kSize || y >= CityMap::kSize)
            return nullptr;
        return map.blockAt(x, y, z);
    };

    // Ground-level surfaces (water, land, pavements, bridge decks) are
    // rendered flat at the city's base height, exactly like the original
    // game: a one-unit CMP step between a road and its pavement is a curb,
    // not a wall.  Only columns that rise clearly above the base height are
    // extruded into buildings.
    auto visualHeight = [&](int surface) {
        if (surface <= 0) return static_cast<int>(baseHeight_);
        return surface <= static_cast<int>(baseHeight_) + 1
                   ? static_cast<int>(baseHeight_)
                   : surface;
    };
    auto visibleHeight = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= CityMap::kSize || y >= CityMap::kSize) {
            return static_cast<int>(baseHeight_);
        }
        const size_t index = static_cast<size_t>(y * CityMap::kSize + x);
        return visualHeight(surface_[index]);
    };
    auto sideTileOf = [](const MapBlock& b, int dir) -> uint8_t {
        switch (dir) {
        case 0: return b.left;
        case 1: return b.right;
        case 2: return b.top;
        default: return b.bottom;
        }
    };
    // Nearest authored wall texture for one side of a column.  GTA1 only
    // stores the facade on the block that owns it; taller columns repeat the
    // same texture for the floors below/above.
    auto pickSideTile = [&](int x, int y, int n, int z, int dir,
                            int& remapOut) -> uint8_t {
        remapOut = 0;
        for (int distance = 0; distance < 6; ++distance) {
            const int below = z - distance;
            if (below >= 0 && below < n) {
                const MapBlock* b = blockAtOrNull(x, y, below);
                if (b) {
                    const uint8_t tile = sideTileOf(*b, dir);
                    if (tile && assets_->sideUV(tile).u1 > assets_->sideUV(tile).u0) {
                        remapOut = b->remapIndex();
                        return tile;
                    }
                }
            }
            if (distance == 0) continue;
            const int above = z + distance;
            if (above >= 0 && above < n) {
                const MapBlock* b = blockAtOrNull(x, y, above);
                if (b) {
                    const uint8_t tile = sideTileOf(*b, dir);
                    if (tile && assets_->sideUV(tile).u1 > assets_->sideUV(tile).u0) {
                        remapOut = b->remapIndex();
                        return tile;
                    }
                }
            }
        }
        return 0;
    };

    auto flipUV = [](TileUV uv, bool tb, bool lr) {
        if (tb) std::swap(uv.v0, uv.v1);
        if (lr) std::swap(uv.u0, uv.u1);
        return uv;
    };

    struct Side {
        int nx, ny;
        float x0, z0, x1, z1;
        float shade;
        int dir;
    };

    // Procedural road paint used by the clean-road renderer: the baked lane
    // tiles are replaced with plain asphalt and continuous paint is laid over
    // them, so dashes align between lanes and intersections stay uncluttered.
    auto roadAt = [&](int x, int y) {
        return roadSurface(x, y) > 0;
    };
    auto paintRect = [&](float ax, float az, float bx, float bz, float y,
                         float r, float g, float b) {
        add(Vec3{ax, y, az}, Vec3{bx, y, az}, Vec3{bx, y, bz},
            Vec3{ax, y, bz}, assets_->whiteUV(), r, g, b, false, true);
    };
    auto addRoadPaint = [&](int cx, int cz, float lidY) {
        const float x0 = static_cast<float>(cx);
        const float x1 = x0 + 1.0f;
        const float z0 = static_cast<float>(cz);
        const float z1 = z0 + 1.0f;
        const float y = lidY + 0.02f;
        const float pr = 0.98f, pg = 0.80f, pb = 0.12f;
        const float inset = 0.055f;
        const float line = 0.05f;

        // Corridor extents: how far the carriageway continues on each side.
        int east = 0, west = 0, south = 0, north = 0;
        while (east < 20 && roadAt(cx + east + 1, cz)) ++east;
        while (west < 20 && roadAt(cx - west - 1, cz)) ++west;
        while (south < 20 && roadAt(cx, cz + south + 1)) ++south;
        while (north < 20 && roadAt(cx, cz - north - 1)) ++north;
        const int widthX = west + east + 1;  // perpendicular for a N-S road
        const int widthZ = north + south + 1; // perpendicular for an E-W road
        const int runZ = widthZ;
        const int runX = widthX;
        const bool vertical = runZ >= 3 && widthX <= 8;
        const bool horizontal = runX >= 3 && widthZ <= 8;
        if (!vertical && !horizontal) return; // junction or open plaza

        // Solid edge paint along the kerbs.
        if (vertical) {
            if (west == 0)
                paintRect(x0 + inset, z0, x0 + inset + line, z1, y, pr, pg, pb);
            if (east == 0)
                paintRect(x1 - inset - line, z0, x1 - inset, z1, y, pr, pg, pb);
        }
        if (horizontal) {
            if (north == 0)
                paintRect(x0, z0 + inset, x1, z0 + inset + line, y, pr, pg, pb);
            if (south == 0)
                paintRect(x0, z1 - inset - line, x1, z1 - inset, y, pr, pg, pb);
        }

        // Lane separators every two cells out from the corridor centre, with
        // the dash phase tied to the along-road cell index so they line up
        // between lanes instead of staggering.
        auto paintAlong = [&](float position, bool alongZ) {
            if (alongZ) {
                if ((cz & 1) != 0) return;
                paintRect(position - line * 0.5f, z0 + 0.16f,
                          position + line * 0.5f, z1 - 0.16f, y, pr, pg, pb);
            } else {
                if ((cx & 1) != 0) return;
                paintRect(x0 + 0.16f, position - line * 0.5f, x1 - 0.16f,
                          position + line * 0.5f, y, pr, pg, pb);
            }
        };
        if (vertical) {
            const float low = static_cast<float>(cx - west);
            const float high = static_cast<float>(cx + east + 1);
            const float centre = (low + high) * 0.5f;
            for (int offset = 0; offset <= 8; offset += 2) {
                for (int side = -1; side <= 1; side += 2) {
                    if (offset == 0 && side == 1) continue;
                    const float position = centre + side * offset;
                    if (position < low + 0.5f || position > high - 0.5f)
                        continue;
                    if (std::floor(position) != static_cast<float>(cx))
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
            const float x0 = static_cast<float>(cellX);
            const float x1 = x0 + 1.0f;
            const float z0 = static_cast<float>(cellY);
            const float z1 = z0 + 1.0f;

            const int n = map.blockCount(cellX, cellY);
            const size_t cellIndex =
                static_cast<size_t>(cellY * CityMap::kSize + cellX);
            const int columnSurface = surface_[cellIndex];
            const int cellTop = visualHeight(columnSurface);

            // Cells without a visible opaque top (open sea, park holes, or
            // columns that only contain transparent decals) need a floor.
            if (n <= 0 || columnSurface == 0) {
                const float floorY = static_cast<float>(baseHeight_);
                TileUV floorUV;
                int floorRotation = 0;
                if (n > 0) {
                    // A decal-only column: borrow the most common neighbouring
                    // surface texture, so a crosswalk sits on asphalt and a
                    // fence sits on grass.
                    if (baseLid_[cellIndex] != 0) {
                        floorUV = assets.lidUV(baseLid_[cellIndex]);
                        floorRotation = baseRot_[cellIndex];
                    }
                } else {
                    const bool isWater = floorWater_[cellIndex] != 0;
                    floorUV = isWater ? waterFloor : grassFloor;
                }
                if (floorUV.u1 <= floorUV.u0) floorUV = fallbackRoof;
                if (floorUV.u1 <= floorUV.u0) floorUV = fallbackRoad;
                if (floorUV.u1 > floorUV.u0) {
                    addLid(Vec3{x0, floorY, z0}, Vec3{x1, floorY, z0},
                           Vec3{x1, floorY, z1}, Vec3{x0, floorY, z1}, floorUV,
                           floorRotation, false);
                }
                if (n <= 0) continue;
            }

            // Draw every block of the column: GTA1 stores trees, road paint
            // and similar decals as transparent "flat" blocks stacked on top
            // of the real surface.  Flat overlays are clamped to the cell's
            // visual top so road markings lie on the tarmac instead of
            // floating one unit above it.
            for (int z = 0; z < n; ++z) {
                const MapBlock* block = blockAtOrNull(cellX, cellY, z);
                if (!block || block->isAir()) continue;

                const bool flat = block->isFlat();
                const float y0 = static_cast<float>(z);
                const float y1 = y0 + 1.0f;

                if (block->lid) {
                    const TileUV uv =
                        assets.lidUV(block->lid, block->remapIndex());
                    if (uv.u1 > uv.u0) {
                        float lidY = y1;
                        if (flat) {
                            lidY = static_cast<float>(cellTop);
                        } else if (z + 1 == columnSurface) {
                            // The column's visible top: flatten ground levels
                            // and keep real building roofs at their height.
                            lidY = static_cast<float>(cellTop);
                        }
                        // Only side textures use the mirror flags; lid tiles
                        // are oriented by the rotation bits alone.  Mirroring
                        // a road lid flips the dash phase and breaks the line
                        // at every cell boundary.
                        // Ground-level road lids.  Intersections always get a
                        // plain asphalt top so the authored edge tiles cannot
                        // paint disconnected hooks across the junction; the
                        // optional clean-road mode also replaces the lane
                        // tiles on straight stretches with procedural paint.
                        const bool isGroundRoad =
                            z + 1 == columnSurface &&
                            columnSurface <= static_cast<int>(baseHeight_) + 1 &&
                            assets.isRoadTile(block->lid);
                        const bool groundRoad = isGroundRoad && cleanRoads_;
                        if (groundRoad) {
                            const TileUV plain = assets.lidUV(
                                assets.plainRoadLid(), block->remapIndex());
                            addLid(Vec3{x0, lidY, z0}, Vec3{x1, lidY, z0},
                                   Vec3{x1, lidY, z1}, Vec3{x0, lidY, z1},
                                   plain,
                                   static_cast<int>(block->rotation()), false);
                            addRoadPaint(cellX, cellY, lidY);
                        } else {
                            addLid(Vec3{x0, lidY, z0}, Vec3{x1, lidY, z0},
                                   Vec3{x1, lidY, z1}, Vec3{x0, lidY, z1},
                                   uv, static_cast<int>(block->rotation()),
                                   flat);
                        }
                    }
                }

                if (!flat) continue;

                // Transparent decals can carry walls too (park railings,
                // hedges, signs).  They stand one unit above the cell's
                // rendered ground.
                const Side sides[4] = {
                    {cellX - 1, cellY, x0, z0, x0, z1, 0.94f, 0},
                    {cellX + 1, cellY, x1, z1, x1, z0, 0.90f, 1},
                    {cellX, cellY - 1, x1, z0, x0, z0, 0.84f, 2},
                    {cellX, cellY + 1, x0, z1, x1, z1, 0.98f, 3},
                };
                for (const Side& side : sides) {
                    const uint8_t tile = sideTileOf(*block, side.dir);
                    if (!tile) continue;
                    const TileUV uv = flipUV(
                        assets.sideUV(tile, block->remapIndex()),
                        block->flipTopBottom(), block->flipLeftRight());
                    if (uv.u1 <= uv.u0) continue;
                    const float wallY = static_cast<float>(cellTop);
                    add(Vec3{side.x0, wallY, side.z0},
                        Vec3{side.x1, wallY, side.z1},
                        Vec3{side.x1, wallY + 1.0f, side.z1},
                        Vec3{side.x0, wallY + 1.0f, side.z0}, uv, side.shade,
                        side.shade, side.shade, true);
                }
            }

            // Solid facades: close every height step from the neighbour's
            // rendered ground up to this column's roof.  Textures are taken
            // from the authored blocks level by level, with a generic wall
            // as the last resort so buildings never appear as floating roofs.
            if (cellTop <= static_cast<int>(baseHeight_)) continue;
            const Side sides[4] = {
                {cellX - 1, cellY, x0, z0, x0, z1, 0.94f, 0},
                {cellX + 1, cellY, x1, z1, x1, z0, 0.90f, 1},
                {cellX, cellY - 1, x1, z0, x0, z0, 0.84f, 2},
                {cellX, cellY + 1, x0, z1, x1, z1, 0.98f, 3},
            };
            for (const Side& side : sides) {
                const int neighbourTop = visibleHeight(side.nx, side.ny);
                if (cellTop <= neighbourTop) continue;
                for (int z = neighbourTop; z < cellTop; ++z) {
                    int remap = 0;
                    const uint8_t tile =
                        pickSideTile(cellX, cellY, n, z, side.dir, remap);
                    TileUV uv = tile ? assets.sideUV(tile, remap) : fallbackWall;
                    if (uv.u1 <= uv.u0) uv = fallbackWall;
                    if (uv.u1 <= uv.u0) continue;
                    add(Vec3{side.x0, static_cast<float>(z), side.z0},
                        Vec3{side.x1, static_cast<float>(z), side.z1},
                        Vec3{side.x1, static_cast<float>(z + 1), side.z1},
                        Vec3{side.x0, static_cast<float>(z + 1), side.z0}, uv,
                        side.shade, side.shade, side.shade, false);
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
