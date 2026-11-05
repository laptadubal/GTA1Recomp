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

// Builds a chunked 2.5D mesh out of a CMP map + GRY tile atlas.
class World {
public:
    static constexpr int kChunkSize = 16;
    static constexpr int kChunks = CityMap::kSize / kChunkSize;

    World() = default;
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    bool build(AssetManager& assets, std::string& error);
    void destroy();

    int surfaceHeight(int cellX, int cellY) const;
    int roadSurface(int cellX, int cellY) const;
    bool isDrivable(float worldX, float worldZ) const;
    Vec3 findSpawn(int preferredX = 108, int preferredY = 108) const;

    const std::vector<CityChunk>& chunks() const { return chunks_; }
    const AssetManager& assets() const { return *assets_; }

private:
    bool buildChunk(int cx, int cy, std::vector<CityVertex>& vertices,
                    std::vector<uint32_t>& indices, GLsizei& opaqueIndices);

    AssetManager* assets_ = nullptr;
    std::vector<CityChunk> chunks_;
    std::vector<uint8_t> surface_; // highest drawn lid, 0 = no visible top
    std::vector<uint8_t> floorWater_; // 1 = water floor for cells without a top
    uint8_t baseHeight_ = 1;       // most common street level in this city

    void addQuad(std::vector<CityVertex>& v, std::vector<uint32_t>& idx,
                 const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3,
                 const TileUV& uv, float r, float g, float b, bool transparent);
};

} // namespace gta
