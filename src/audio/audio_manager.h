#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include <SDL.h>

#include "core/config.h"

namespace gta {

// SDL2 audio output. Original SDT samples provide effects and vehicle engines;
// a synthesized engine remains available when a matching sample is missing.
class AudioManager {
public:
    AudioManager() = default;
    ~AudioManager();
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    bool init(const AudioConfig& config, std::string& error);
    void shutdown();

    void setPaused(bool paused);
    void setMasterVolume(float volume);
    void setMusicVolume(float volume);
    void setEngineVolume(float volume);
    void setMusicEnabled(bool enabled);
    void setEngine(float speed, float throttle, bool crashing, int sfxId = -1);
    // Street ambience (wind + distant traffic rumble).  The original plays
    // no music while the city is open, only environmental noise, so this
    // bed is what makes the streets feel inhabited.
    void setAmbient(float level);
    void playPickupSound();

    bool playMusicFile(const std::string& path, std::string& error);
    void stopMusic();
    bool musicLoaded() const { return !musicSamples_.empty(); }

    // Original .SDT/.RAW sound bank (8-bit unsigned mono samples).
    bool loadSfxBank(const std::string& sdtPath, std::string& error);
    void playSfx(int id, float volume = 0.8f);
    void playUiClick();
    void playCrashSound();
    bool sfxLoaded() const { return !sfxRaw_.empty(); }

    bool available() const { return device_ != 0; }

private:
    static void audioCallback(void* userdata, Uint8* stream, int length);
    void mix(Sint16* out, int samples);

    SDL_AudioDeviceID device_ = 0;
    SDL_AudioSpec spec_{};

    std::atomic<float> volume_{0.7f};
    std::atomic<float> musicVolume_{0.60f};
    std::atomic<float> engineVolume_{0.25f};
    std::atomic<float> speed_{0.0f};
    std::atomic<float> throttle_{0.0f};
    std::atomic<float> crash_{0.0f};
    std::atomic<float> ambient_{0.0f};
    std::atomic<int> engineSfxId_{-1};
    std::atomic<bool> paused_{false};
    std::atomic<bool> musicEnabled_{true};
    std::atomic<int> pickupRequests_{0};

    std::vector<Sint16> musicSamples_;
    int musicChannels_ = 2;
    int musicRate_ = 22050;
    double musicPosition_ = 0.0;

    struct SfxEntry {
        uint32_t start = 0;
        uint32_t size = 0;
        uint32_t rate = 22050;
    };
    struct SfxVoice {
        int id = -1;
        double position = 0.0;
        float volume = 0.0f;
    };
    std::vector<uint8_t> sfxRaw_;
    std::vector<SfxEntry> sfxEntries_;
    SfxVoice sfxVoices_[8];
    int activeEngineSfxId_ = -1;
    double engineSfxPosition_ = 0.0;

    double phase_ = 0.0;
    double pickupPhase_ = 0.0;
    float pickupEnvelope_ = 0.0f;
    float lowpass_ = 0.0f;
    float ambRumble_ = 0.0f;  // low-passed traffic rumble
    float ambAir_ = 0.0f;     // light "wind in the air" noise
    double ambWindPhase_ = 0.0;
    uint32_t noiseState_ = 0x12345678u;
};

} // namespace gta
