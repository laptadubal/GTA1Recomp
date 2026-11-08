#pragma once

#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
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
    // Advance GRY's synchronous tile animations on the render thread.
    void updateAnimations(float dt);
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
    // True for road lids whose art contains structural detail (kerb stones,
    // concrete medians, railings, painted corner curves).  The clean-road
    // renderer keeps these and only replaces plain lane-paint tiles.
    bool isStructuralLid(int index1Based) const;
    bool isWaterTile(int index1Based) const;
    bool isGrassTile(int index1Based) const;
    // True when the tile art contains transparency-key pixels.  The original
    // engine colour-keys those against whatever is behind, so such faces are
    // routed to the alpha-test pass instead of the opaque one.
    bool sideHasAlpha(int index1Based) const;
    bool lidHasAlpha(int index1Based) const;
    // Average palette colour of a lid tile, used by the clean city map.
    void lidColor(int index1Based, uint8_t& r, uint8_t& g, uint8_t& b) const;

    // Representative lids used when a map block has no texture of its own.
    int roadFallbackLid() const { return roadFallbackLid_; }
    int roofFallbackLid() const { return roofFallbackLid_; }
    int waterFallbackLid() const { return waterFallbackLid_; }
    int grassFallbackLid() const { return grassFallbackLid_; }

    // Sprite texture cache (RGBA, alpha = 0 for palette index 0).
    // pass remap != 0 to get a variant recoloured through the GRY remap
    // tables (pedestrian/character clothing variants); those are kept in a
    // bounded LRU cache separate from the main per-index cache.
    GLuint spriteTexture(int grySpriteIndex, int remap = 0);
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
    void uploadAnimatedTile(const GryAnimation& animation, int phase);
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
    // Per-atlas-slot flag: the tile art contains palette-index-0 pixels.
    std::vector<uint8_t> slotHasAlpha_;
    std::vector<TileUV> auxUVs_;
    std::vector<uint8_t> animationPhases_;
    double animationCycles_ = 0.0;
    int variantStride_ = 0; // pixels per palette variant in the atlas
    TileUV whiteUV_{};
    int plainRoadLid_ = 1;
    std::vector<uint8_t> lidIsRoad_;
    std::vector<uint8_t> lidIsStructural_;
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

    // LRU cache for remapped sprite variants, keyed by (index, remap).
    // Sized to hold the full crowd's working set (~70 peds x 8 frames)
    // without frame-to-frame evictions.
    static constexpr uint32_t kMaxRemapSprites = 2048;
    std::unordered_map<uint32_t, CachedSprite> spriteRemapCache_;
    std::list<uint32_t> spriteRemapLru_;
    std::unordered_map<uint32_t, std::list<uint32_t>::iterator> spriteRemapLruIndex_;
};

} // namespace gta
