#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/math3d.h"
#include "data/asset_manager.h"
#include "graphics/gl.h"

namespace gta {

struct CityVertex {
    float x, y, z;
    float u, v;
    float r, g, b, a;
};

struct CityChunk {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ibo = 0;
    GLsizei indexCount = 0;
    // Every chunk stores its opaque faces first and its alpha-blended (flat
    // map decal) faces second, so the world can be drawn in two passes.
    GLsizei opaqueIndexCount = 0;
    int cx = 0, cy = 0;
    float minX = 0, minZ = 0, maxX = 0, maxZ = 0;
};

struct TrafficRoadLevel {
    int rawHeight = 0;
    uint8_t directionMask = 0;
    uint8_t slope = 0;
};

// Builds a chunked 2.5D mesh out of a CMP map + GRY tile atlas.
class World {
public:
    static constexpr int kChunkSize = 16;
    static constexpr int kChunks = CityMap::kSize / kChunkSize;

    World() = default;
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    void setCleanRoads(bool enabled) { cleanRoads_ = enabled; }
    bool build(AssetManager& assets, std::string& error);
    void destroy();

    int surfaceHeight(int cellX, int cellY) const;
    int roadSurface(int cellX, int cellY) const;
    // Surface tagged as road (block type 2), excluding pavement tiles which
    // share asphalt-like graphics and are still driveable for the player.
    // All type-2 road lids in a column, ordered from highest to lowest.
    const std::vector<TrafficRoadLevel>& trafficRoadLevels(int cellX,
                                                           int cellY) const;
    // Highest authored road deck in a cell (for single-surface queries).
    int trafficSurface(int cellX, int cellY) const;
    int trafficSurfaceAt(int cellX, int cellY, int rawHeight) const;
    bool isPassableAtRoadLevel(float worldX, float worldZ,
                               int rawHeight) const;
    // Resolve the adjacent road level using both the source cell's direction
    // flags and the authored ramp geometry. Returns 0 when no legal join exists.
    int trafficNextRawHeight(int fromX, int fromY, int fromRawHeight,
                             int dirX, int dirY,
                             bool obeyDirectionMask = true) const;
    // Direction flags may be stored in the block directly above the road art.
    // A zero mask means this cell has no authored direction restriction.
    uint8_t roadDirectionMask(int cellX, int cellY) const;
    bool roadDirectionAllowed(int cellX, int cellY, int dirX, int dirY) const;
    // Rendered height of a ground column: water, land, pavements and bridge
    // decks are flattened to the city base height, buildings keep their own.
    int visualHeight(int surface) const;
    int baseHeight() const { return baseHeight_; }
    bool isDrivable(float worldX, float worldZ) const;
    // Ground the player's car can physically drive over: roads, pavements,
    // grass and bridge decks, but not buildings or open water.  GTA1 lets you
    // bump over kerbs, so this is deliberately wider than roadSurface().
    bool isPassable(float worldX, float worldZ) const;
    Vec3 findSpawn(int preferredX = 108, int preferredY = 108) const;

    const std::vector<CityChunk>& chunks() const { return chunks_; }
    const AssetManager& assets() const { return *assets_; }

private:
    bool buildChunk(int cx, int cy, std::vector<CityVertex>& vertices,
                    std::vector<uint32_t>& indices, GLsizei& opaqueIndices);

    AssetManager* assets_ = nullptr;
    std::vector<CityChunk> chunks_;
    std::vector<std::vector<TrafficRoadLevel>> trafficRoadLevels_;
    std::vector<uint8_t> surface_; // highest drawn lid, 0 = no visible top
    std::vector<uint8_t> floorWater_; // 1 = water floor for cells without a top
    // Nearest authored lid/rotation, used as the base under flat decals.
    std::vector<uint8_t> baseLid_;
    std::vector<uint8_t> baseRot_;
    uint8_t baseHeight_ = 1;       // most common street level in this city
    bool cleanRoads_ = false;

    void addQuad(std::vector<CityVertex>& v, std::vector<uint32_t>& idx,
                 const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3,
                 const TileUV& uv, float r, float g, float b, bool transparent,
                 bool faceUp = false);
};

} // namespace gta
