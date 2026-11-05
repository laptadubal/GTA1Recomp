#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "audio/audio_manager.h"
#include "core/config.h"
#include "data/asset_manager.h"
#include "game/vehicle.h"
#include "game/world.h"
#include "graphics/bitmap_font.h"
#include "graphics/camera.h"
#include "graphics/font.h"
#include "graphics/renderer.h"
#include "input/input_manager.h"

namespace gta {

class Game {
public:
    Game() = default;
    ~Game();
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    bool init(Renderer& renderer, const GameConfig& config, std::string& error);
    void shutdown();

    void update(float dt, InputManager& input, AudioManager& audio);
    void render(Renderer& renderer);

    const AssetManager& assets() const { return assets_; }
    const Vehicle& vehicle() const { return vehicle_; }
    const World& world() const { return world_; }

    bool screenshotRequested() const { return screenshotRequested_; }
    void clearScreenshotRequest() { screenshotRequested_ = false; }
    bool quitRequested() const { return quit_; }

    void setDemo(bool enabled);
    bool showScreen(const std::string& name);
    GameConfig& config() { return config_; }

private:
    enum class Screen {
        Intro,
        Cutscene,
        MainMenu,
        Settings,
        Controls,
        Playing,
        CityMap,
        Paused,
        PausedSettings,
        MissionComplete
    };

    enum class MissionType { Reach, StealDeliver, Pickup, KillPed, DestroyCar };
    enum class MissionPhase { None, GoToTarget, Deliver };
    enum class WeaponType { Pistol = 0, Uzi, Flamethrower, Rocket, Count };
    enum class PickupKind { Weapon, Health, Armor };

    struct MissionPlan {
        std::string briefing;
        MissionType type = MissionType::Reach;
        float timeLimit = 90.0f;
        int reward = 500;
    };

    struct MissionCar {
        bool active = false;
        Vec3 pos{};
        float yaw = 0.0f;
        int spriteIndex = 0;
        int pixelWidth = 48;
        int pixelHeight = 32;
        float width = 0.8f;
        float height = 0.6f;
        int health = 3;
        float shake = 0.0f;
        float hitCooldown = 0.0f;
    };

    struct MissionPed {
        bool active = false;
        Vec3 pos{};
        float yaw = 0.0f;
        int spriteIndex = 0;
        float width = 0.35f;
        float height = 0.5f;
        bool alive = true;
        float health = 100.0f;
    };

    struct Pickup {
        Vec3 pos;
        bool active = true;
    };

    struct Phone {
        Vec3 pos;
        bool active = true;
    };

    struct TrafficCar {
        Vec3 pos{};
        int cellX = 0;
        int cellY = 0;
        int dirX = 0;
        int dirY = -1;
        float yaw = 0.0f;
        float speed = 4.5f;
        int spriteIndex = 0;
        float width = 1.0f;
        float height = 1.0f;
        float stuck = 0.0f;
        float health = 100.0f;
        bool dead = false;
        float respawnTimer = 0.0f;
    };

    struct WorldProp {
        Vec3 pos{};
        float yaw = 0.0f;
        int spriteIndex = 0;
        float width = 1.0f;
        float height = 1.0f;
    };

    struct Pedestrian {
        Vec3 pos{};
        int cellX = 0;
        int cellY = 0;
        int dirX = 0;
        int dirY = -1;
        float yaw = 0.0f;
        float speed = 0.7f;
        int spriteIndex = 0;
        float width = 0.3f;
        float height = 0.4f;
        float stuck = 0.0f;
        float health = 60.0f;
        bool dead = false;
        float deadTimer = 0.0f;
        float panicTimer = 0.0f;
    };

    struct Projectile {
        Vec3 pos{};
        Vec3 velocity{};
        float life = 1.6f;
        int damage = 20;
        bool fromPlayer = true;
        bool explosive = false;
        bool flame = false;
        float radius = 0.18f;
    };

    struct Particle {
        Vec3 pos{};
        Vec3 velocity{};
        float life = 0.6f;
        float maxLife = 0.6f;
        float size = 0.16f;
        float r = 1.0f, g = 1.0f, b = 1.0f;
        float gravity = 0.0f;
        float grow = 0.0f;
        bool additive = false;
    };

    struct WeaponPickup {
        Vec3 pos{};
        int spriteIndex = 0;
        PickupKind kind = PickupKind::Weapon;
        WeaponType weapon = WeaponType::Pistol;
        int amount = 10;
        bool active = true;
        float respawnTimer = 0.0f;
    };

    struct PoliceCar {
        Vec3 pos{};
        float yaw = 0.0f;
        float speed = 0.0f;
        int spriteIndex = 0;
        float health = 140.0f;
        float fireCooldown = 1.0f;
        float stuck = 0.0f;
        float spawnCopTimer = 1.0f;
        bool active = false;
    };

    struct Cop {
        Vec3 pos{};
        float yaw = 0.0f;
        int spriteIndex = 811;
        float health = 55.0f;
        float fireCooldown = 1.2f;
        float animTime = 0.0f;
        float deadTimer = 0.0f;
        bool active = false;
        bool dead = false;
    };

    enum class MissionState { Idle, Briefing, Active, Result };

    bool loadCity(const std::string& city, std::string& error);
    bool setupCar(std::string& error);
    void startGame();
    void beginPlay();
    void startSelectedGame();
    void resetCar();
    void resetMission();
    void updateOnFoot(float dt, InputManager& input, AudioManager& audio);
    void drawPlayer(Renderer& renderer);
    Vec3 playerPosition() const {
        return inCar_ ? vehicle_.position() : footPos_;
    }
    bool nearVehicle() const;
    void spawnPickups();
    void spawnTraffic();
    void updateTraffic(float dt);
    void drawTraffic(Renderer& renderer);
    void spawnPeds();
    void updatePeds(float dt);
    void drawPeds(Renderer& renderer);
    void spawnProps();
    void drawProps(Renderer& renderer);
    int randomCarSprite();
    int randomPedSprite();

    // Weapons, damage, police and effects.
    void updateCombat(float dt, InputManager& input, AudioManager& audio);
    void updateProjectiles(float dt, AudioManager& audio);
    void updateParticles(float dt);
    void updatePolice(float dt, AudioManager& audio);
    void updateWeaponPickups(float dt, AudioManager& audio);
    void spawnWeaponPickups();
    void fireWeapon(InputManager& input, AudioManager& audio);
    void spawnParticle(const Vec3& pos, const Vec3& velocity, float life,
                       float size, float r, float g, float b, float gravity = 0.0f,
                       float grow = 0.0f, bool additive = false);
    void spawnBlood(const Vec3& pos, int amount);
    void spawnSparks(const Vec3& pos);
    void explodeAt(const Vec3& pos, float radius, int damage,
                   AudioManager& audio, bool fromPlayer);
    void damagePlayer(int amount, AudioManager& audio);
    void damageCar(float amount, AudioManager& audio);
    void damagePedestrian(int index, int amount, AudioManager& audio);
    void damageTrafficCar(int index, int amount, AudioManager& audio);
    void damagePoliceCar(int index, float amount, AudioManager& audio);
    void damageCop(int index, int amount, AudioManager& audio);
    void applyWanted(int stars, const std::string& reason);
    void clearPolice();
    void startWasted(AudioManager& audio);
    void startBusted(AudioManager& audio);
    void respawnPlayer(AudioManager& audio);
    void carjackNearest(AudioManager& audio);
    int nearestTrafficCar(float maxDistance) const;
    Vec3 aimDirection() const;
    int weaponIcon() const;
    std::string weaponName() const;
    void drawProjectiles(Renderer& renderer);
    void drawParticles(Renderer& renderer);
    void drawWeaponPickups(Renderer& renderer);
    void drawPolice(Renderer& renderer);
    void drawCops(Renderer& renderer);
    void drawWorldBillboard(Renderer& renderer, GLuint texture, const Vec3& pos,
                            float size, float r, float g, float b, float a);
    void drawWorldSprite(Renderer& renderer, GLuint texture, const Vec3& pos,
                         float yaw, float width, float height, float yOffset,
                         float r = 1.0f, float g = 1.0f, float b = 1.0f,
                         float a = 1.0f);
    void loadMissionData();
    void buildMissionPlan();
    void pickNearestPhone();
    void startBriefing();
    void beginMission();
    void completeMission(bool success, AudioManager& audio);
    void updateMission(float dt, InputManager& input, AudioManager& audio);
    void updateMissionEntities(float dt);
    void drawMissionMarkers(Renderer& renderer);
    void drawMissionTargets(Renderer& renderer);
    void drawMissionUi(Renderer& renderer);
    void drawObjectiveArrow(Renderer& renderer, const Vec3& target);
    bool chooseMissionLocation(float minDistance, float maxDistance, Vec3& out);
    bool chooseRoadLocation(const Vec3& origin, float minDistance,
                            float maxDistance, Vec3& out);
    void clearMissionEntities();
    void takeMissionCar();
    void pickupMissionPassenger();
    void setObjective(const std::string& text);
    std::string wrapText(const std::string& text, size_t width) const;
    std::string wrapTextWidth(const std::string& text, float maxWidth,
                              float scale, const BitmapFont& font) const;
    void playMusicTrack(int index, AudioManager& audio);
    void loadMusicPlaylist();
    void loadOriginalMenuAssets();
    GLuint loadRawTexture(const std::string& path, int width, int height);
    GLuint loadIndexedTexture(const std::string& indexFile,
                              const std::string& paletteFile, int width,
                              int height, bool paletteSixBit);
    void setStatus(const std::string& text);

    void updateIntro(float dt, InputManager& input, AudioManager& audio);
    void updateCutscene(float dt, InputManager& input, AudioManager& audio);
    void startCutscene(int index);
    int cutsceneForCity() const;
    void updateMainMenu(float dt, InputManager& input, AudioManager& audio);
    void updateSettings(float dt, InputManager& input, AudioManager& audio,
                        bool fromPause);
    void updateControls(InputManager& input, AudioManager& audio);
    void updatePlaying(float dt, InputManager& input, AudioManager& audio);
    void updateCityMap(InputManager& input, AudioManager& audio);
    void updatePaused(InputManager& input, AudioManager& audio);
    void updateMissionComplete(InputManager& input, AudioManager& audio);
    void handleGlobalInput(InputManager& input, AudioManager& audio);
    void updateMenuCamera(float dt);

    struct MenuFrame {
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float scale = 1.0f;
    };
    int menuHitTest(int count, float topFraction, float x, float y) const;
    int menuHitTestFrame(int count, float firstY43, float spacing43,
                         float xMin43, float xMax43, float x, float y) const;
    MenuFrame menuFrame() const;
    float uiScale() const;
    float menuFirstY(int count, float topFraction) const;
    float menuItemWidth() const;
    float menuItemSpacing() const;

    MenuFrame drawOriginalBackdrop(Renderer& renderer, bool showLogo);
    void drawOriginalText(Renderer& renderer, const MenuFrame& frame,
                          const std::string& text, float x, float y,
                          float scale, float r, float g, float b, float a,
                          bool centered);

    void renderWorldScene(Renderer& renderer, bool withCar, bool withPickups);
    void renderIntro(Renderer& renderer);
    void renderCutscene(Renderer& renderer);
    void renderMainMenu(Renderer& renderer);
    void renderSettings(Renderer& renderer, bool fromPause);
    void renderControls(Renderer& renderer);
    void renderCityMap(Renderer& renderer);
    void renderPause(Renderer& renderer);
    void renderMissionComplete(Renderer& renderer);

    void drawWorld(Renderer& renderer);
    void drawCar(Renderer& renderer);
    void drawPickups(Renderer& renderer);
    void drawHud(Renderer& renderer);
    void drawMinimap(Renderer& renderer);
    void drawStatusBanner(Renderer& renderer, const std::string& text,
                          float alpha, float r, float g, float b);
    void drawHudText(Renderer& renderer, const BitmapFont& font,
                     const std::string& text, float x, float y, float scale,
                     float r, float g, float b, float a, bool centered,
                     bool outline);
    void drawColoredTriangle(Renderer& renderer, const Vec3& a, const Vec3& b,
                             const Vec3& c, float r, float g, float bl,
                             float alpha);
    GLuint createMinimapFrameTexture(int size);
    void drawRect(Renderer& renderer, float x, float y, float w, float h,
                  float r, float g, float b, float a);
    void drawTexturedRect(Renderer& renderer, GLuint texture, float x, float y,
                          float w, float h, float u0, float v0, float u1,
                          float v1, float r, float g, float b, float a);
    void drawTexturedRectMasked(Renderer& renderer, GLuint texture, float x,
                                float y, float w, float h, float u0, float v0,
                                float u1, float v1, float r, float g, float b,
                                float a, float maskCenterU, float maskCenterV,
                                float maskRadius);
    void drawText(Renderer& renderer, const std::string& text, float x, float y,
                  float scale, float r, float g, float b, float a,
                  bool centered = false, bool shadow = true);
    void drawMenuItem(Renderer& renderer, int index, int count,
                      float topFraction, const std::string& label,
                      const std::string& value, bool selected,
                      bool showArrows);
    float textWidth(const std::string& text, float scale) const;

    AssetManager assets_;
    World world_;
    Vehicle vehicle_;
    Camera camera_;
    Font font_;
    BitmapFont menuFont_;
    BitmapFont bigFont_;
    BitmapFont missionFont_;
    BitmapFont missionHeaderFont_;
    BitmapFont keyFont_;
    BitmapFont cutTextFont_;
    GLuint menuUpper_ = 0;
    GLuint menuLower_ = 0;
    GLuint logoTextures_[8] = {};
    GLuint cutsceneTextures_[6] = {};
    GLuint dmaLogo_ = 0;
    GLuint bmgLogo_ = 0;
    std::unordered_map<std::string, std::string> fxt_;
    bool originalAssets_ = false;
    GameConfig config_;
    Renderer* renderer_ = nullptr;

    Screen screen_ = Screen::Intro;
    Screen settingsReturn_ = Screen::MainMenu;
    bool quit_ = false;
    bool screenshotRequested_ = false;
    bool showDebug_ = false;
    bool inCar_ = false;
    Vec3 footPos_;
    float footYaw_ = 0.0f;
    float footAnimTime_ = 0.0f;
    int footFrame_ = 620;
    bool demo_ = false;
    bool wasCrashing_ = false;
    float demoTime_ = 0.0f;
    float demoBriefTimer_ = 0.0f;
    float introTime_ = 0.0f;
    float menuTime_ = 0.0f;
    float worldTime_ = 0.0f;
    float fps_ = 60.0f;
    float screenshotFlash_ = 0.0f;

    int menuIndex_ = 0;
    int settingsIndex_ = 0;
    int cameraMode_ = 0;
    int carIndex_ = 0;
    int spriteIndex_ = 0;
    int carModel_ = 0;
    std::string carLabel_ = "CAR";
    std::string selectedCity_ = "NYC";
    std::string lastCity_ = "NYC";
    std::string statusMessage_;
    float statusTimer_ = 0.0f;

    std::vector<Pickup> pickups_;
    int collected_ = 0;
    int totalPickups_ = 10;
    int score_ = 0;
    float missionTime_ = 0.0f;
    bool missionComplete_ = false;
    bool minimapEnabled_ = true;

    std::vector<TrafficCar> traffic_;
    std::vector<Pedestrian> pedestrians_;
    std::vector<WorldProp> props_;
    std::vector<Projectile> projectiles_;
    std::vector<Particle> particles_;
    std::vector<WeaponPickup> weaponPickups_;
    std::vector<PoliceCar> policeCars_;
    std::vector<Cop> cops_;
    float playerHealth_ = 100.0f;
    float playerArmor_ = 0.0f;
    float hurtFlash_ = 0.0f;
    float wastedTimer_ = 0.0f;
    float bustedTimer_ = 0.0f;
    float deathFade_ = 0.0f;
    WeaponType weapon_ = WeaponType::Pistol;
    int ammo_[static_cast<int>(WeaponType::Count)] = {40, 0, 0, 0};
    float fireCooldown_ = 0.0f;
    float muzzleFlash_ = 0.0f;
    float carHealth_ = 100.0f;
    float carHealthMax_ = 100.0f;
    float carSmokeTimer_ = 0.0f;
    float lastCarSpeed_ = 0.0f;
    int wantedLevel_ = 0;
    float wantedDecay_ = 0.0f;
    float wantedFlash_ = 0.0f;
    float policeSpawnTimer_ = 2.0f;
    float sirenTimer_ = 0.0f;
    std::vector<Phone> phones_;
    std::vector<MissionPlan> missions_;
    int activePhone_ = 0;
    int targetPhone_ = -1;
    int missionIndex_ = 0;
    MissionState missionState_ = MissionState::Idle;
    MissionType missionType_ = MissionType::Reach;
    MissionPhase missionPhase_ = MissionPhase::None;
    MissionCar missionCar_;
    MissionPed missionPed_;
    Vec3 missionSource_{};
    Vec3 missionDest_{};
    bool missionCarTaken_ = false;
    bool hasPassenger_ = false;
    float missionTimeLimit_ = 0.0f;
    int missionReward_ = 500;
    std::string briefingText_;
    std::string objectiveText_;
    std::string resultText_;
    float resultTimer_ = 0.0f;
    float missionTimeLeft_ = 0.0f;
    Vec3 targetPos_{};
    float phoneHintTimer_ = 0.0f;
    float phoneRingTimer_ = 0.0f;
    float missionFlash_ = 0.0f;
    float hintFade_ = 0.0f;
    int cutsceneIndex_ = 0;
    int cutsceneLine_ = 0;
    float cutsceneLineTimer_ = 0.0f;
    bool cutsceneThenPlay_ = false;
    bool cutsceneFinished_ = false;

    std::mt19937 rng_{std::random_device{}()};

    std::vector<std::string> musicTracks_;
    int musicTrack_ = 0;
    bool musicStarted_ = false;
    bool menuMusicStarted_ = false;
    bool cutsceneMusicStarted_ = false;
    bool pendingGameMusic_ = false;

    Vec3 menuCenter_{128.5f, 1.0f, 128.5f};

    GLuint carVao_ = 0;
    GLuint carVbo_ = 0;
    GLsizeiptr carCapacity_ = 0;
    GLuint quadVao_ = 0;
    GLuint quadVbo_ = 0;
    GLuint minimapFrame_ = 0;
    int minimapFrameSize_ = 0;
};

} // namespace gta
