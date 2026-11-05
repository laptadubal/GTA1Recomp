#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "data/formats.h"
#include "graphics/gl.h"

namespace gta {

struct TileUV {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
};

// Loads GTADATA/CMP/GRY and uploads the city tile atlas to the GPU.
class AssetManager {
public:
    AssetManager() = default;
    ~AssetManager();
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    bool load(const std::string& dataDir, const std::string& city,
              std::string& error);
    // Releases all GPU resources.  Safe to call more than once and must be
    // called while the OpenGL context is still current.
    void destroy();

    const CityMap& map() const { return map_; }
    const GryFile& gry() const { return gry_; }
    const std::string& dataDir() const { return dataDir_; }
    const std::string& cityName() const { return cityName_; }

    GLuint atlasTexture() const { return atlas_; }
    int atlasWidth() const { return atlasW_; }
    int atlasHeight() const { return atlasH_; }
    GLuint minimapTexture() const { return minimap_; }

    // remap selects one of the four palette variants stored in the atlas.
    TileUV sideUV(int index1Based, int remap = 0) const;
    TileUV lidUV(int index1Based, int remap = 0) const;
    // A pure-white patch in the atlas, used for the procedural road paint.
    const TileUV& whiteUV() const { return whiteUV_; }
    // Plain asphalt lid (no lane paint) used by the clean-road renderer.
    int plainRoadLid() const { return plainRoadLid_; }

    // True for lid tiles that look like asphalt roads (dark with lane
    // markings).  Classified from the palette at load time so all three
    // cities/styles work without a hard-coded tile table.
    bool isRoadTile(int index1Based) const;
    bool isWaterTile(int index1Based) const;
    bool isGrassTile(int index1Based) const;
    // Average palette colour of a lid tile, used by the clean city map.
    void lidColor(int index1Based, uint8_t& r, uint8_t& g, uint8_t& b) const;

    // Representative lids used when a map block has no texture of its own.
    int roadFallbackLid() const { return roadFallbackLid_; }
    int roofFallbackLid() const { return roofFallbackLid_; }
    int waterFallbackLid() const { return waterFallbackLid_; }
    int grassFallbackLid() const { return grassFallbackLid_; }

    // Sprite texture cache (RGBA, alpha = 0 for palette index 0).
    GLuint spriteTexture(int grySpriteIndex);
    int spriteWidth(int grySpriteIndex) const;
    int spriteHeight(int grySpriteIndex) const;

    GLuint carSpriteTexture(int grySpriteIndex) {
        return spriteTexture(grySpriteIndex);
    }
    int carSpriteWidth(int grySpriteIndex) const {
        return spriteWidth(grySpriteIndex);
    }
    int carSpriteHeight(int grySpriteIndex) const {
        return spriteHeight(grySpriteIndex);
    }

    static std::string findDataDir(const std::string& explicitPath);

private:
    bool buildAtlas(std::string& error);
    bool buildMinimap(std::string& error);
    void classifyRoadLids();
    TileUV remapped(TileUV uv, int remap) const;

    std::string dataDir_;
    std::string cityName_;
    CityMap map_;
    GryFile gry_;

    GLuint atlas_ = 0;
    GLuint minimap_ = 0;
    int atlasW_ = 0;
    int atlasH_ = 0;
    int sideSlots_ = 0;
    int lidSlots_ = 0;
    std::vector<TileUV> sideUVs_;
    std::vector<TileUV> lidUVs_;
    std::vector<TileUV> auxUVs_;
    int variantStride_ = 0; // pixels per palette variant in the atlas
    TileUV whiteUV_{};
    int plainRoadLid_ = 1;
    std::vector<uint8_t> lidIsRoad_;
    std::vector<uint8_t> lidIsWater_;
    std::vector<uint8_t> lidIsGrass_;
    std::vector<uint8_t> lidR_;
    std::vector<uint8_t> lidG_;
    std::vector<uint8_t> lidB_;
    int roadFallbackLid_ = 1;
    int roofFallbackLid_ = 1;
    int waterFallbackLid_ = 1;
    int grassFallbackLid_ = 1;

    struct CachedSprite {
        GLuint texture = 0;
        int w = 0;
        int h = 0;
    };
    std::vector<CachedSprite> spriteCache_;
};

} // namespace gta
