#include "audio/audio_manager.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace gta {

namespace {
constexpr int kSampleRate = 44100;
constexpr int kChannels = 2;
constexpr int kBufferSamples = 512;
constexpr float kTwoPi = 6.28318530717958647692f;
} // namespace

AudioManager::~AudioManager() { shutdown(); }

bool AudioManager::init(const AudioConfig& config, std::string& error) {
    shutdown();
    if (!config.enabled) return true;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        error = std::string("SDL audio initialisation failed: ") + SDL_GetError();
        return false;
    }

    SDL_AudioSpec want{};
    want.freq = kSampleRate;
    want.format = AUDIO_S16SYS;
    want.channels = kChannels;
    want.samples = kBufferSamples;
    want.callback = &AudioManager::audioCallback;
    want.userdata = this;

    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &spec_,
                                  SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!device_) {
        error = std::string("no audio device available: ") + SDL_GetError();
        return false;
    }

    volume_.store(std::clamp(config.masterVolume, 0.0f, 1.0f));
    musicVolume_.store(std::clamp(config.musicVolume, 0.0f, 1.0f));
    engineVolume_.store(std::clamp(config.engineVolume, 0.0f, 1.0f));
    musicEnabled_.store(config.music);
    SDL_PauseAudioDevice(device_, 0);
    return true;
}

void AudioManager::shutdown() {
    if (device_) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
    musicSamples_.clear();
}

void AudioManager::setPaused(bool paused) { paused_.store(paused); }

void AudioManager::setMasterVolume(float volume) {
    volume_.store(std::clamp(volume, 0.0f, 1.0f));
}

void AudioManager::setMusicVolume(float volume) {
    musicVolume_.store(std::clamp(volume, 0.0f, 1.0f));
}

void AudioManager::setEngineVolume(float volume) {
    engineVolume_.store(std::clamp(volume, 0.0f, 1.0f));
}

void AudioManager::setMusicEnabled(bool enabled) { musicEnabled_.store(enabled); }

void AudioManager::playPickupSound() {
    if (sfxLoaded()) {
        playSfx(19, 0.8f);
    } else {
        pickupRequests_.fetch_add(1);
    }
}

void AudioManager::setEngine(float speed, float throttle, bool crashing) {
    speed_.store(std::abs(speed));
    throttle_.store(std::clamp(throttle, -1.0f, 1.0f));
    if (crashing) crash_.store(1.0f);
}

void AudioManager::setAmbient(float level) {
    ambient_.store(std::clamp(level, 0.0f, 1.0f));
}

bool AudioManager::playMusicFile(const std::string& path, std::string& error) {
    SDL_AudioSpec wavSpec{};
    Uint8* wav = nullptr;
    Uint32 length = 0;
    if (SDL_LoadWAV(path.c_str(), &wavSpec, &wav, &length) == nullptr) {
        error = std::string("cannot load music: ") + SDL_GetError();
        return false;
    }

    std::vector<Sint16> samples;
    SDL_AudioCVT cvt;
    const int built = SDL_BuildAudioCVT(&cvt, wavSpec.format, wavSpec.channels,
                                       wavSpec.freq, AUDIO_S16SYS,
                                       wavSpec.channels, wavSpec.freq);
    if (built < 0) {
        SDL_FreeWAV(wav);
        error = std::string("cannot convert music: ") + SDL_GetError();
        return false;
    }
    if (built == 0) {
        const size_t count = length / sizeof(Sint16);
        samples.assign(reinterpret_cast<Sint16*>(wav),
                       reinterpret_cast<Sint16*>(wav) + count);
    } else {
        cvt.len = static_cast<int>(length);
        cvt.buf = static_cast<Uint8*>(SDL_malloc(
            static_cast<size_t>(length) * static_cast<size_t>(cvt.len_mult)));
        if (!cvt.buf) {
            SDL_FreeWAV(wav);
            error = "out of memory loading music";
            return false;
        }
        std::memcpy(cvt.buf, wav, length);
        if (SDL_ConvertAudio(&cvt) != 0) {
            SDL_free(cvt.buf);
            SDL_FreeWAV(wav);
            error = std::string("cannot convert music: ") + SDL_GetError();
            return false;
        }
        samples.assign(reinterpret_cast<Sint16*>(cvt.buf),
                       reinterpret_cast<Sint16*>(cvt.buf) +
                           cvt.len_cvt / sizeof(Sint16));
        SDL_free(cvt.buf);
    }
    SDL_FreeWAV(wav);

    if (samples.empty()) {
        error = "music file contains no samples";
        return false;
    }

    if (device_) SDL_LockAudioDevice(device_);
    musicSamples_ = std::move(samples);
    musicChannels_ = std::clamp(static_cast<int>(wavSpec.channels), 1, 2);
    musicRate_ = wavSpec.freq > 0 ? wavSpec.freq : kSampleRate;
    musicPosition_ = 0.0;
    if (device_) SDL_UnlockAudioDevice(device_);
    return true;
}

void AudioManager::stopMusic() {
    if (device_) SDL_LockAudioDevice(device_);
    musicSamples_.clear();
    musicPosition_ = 0.0;
    if (device_) SDL_UnlockAudioDevice(device_);
}

bool AudioManager::loadSfxBank(const std::string& sdtPath, std::string& error) {
    std::ifstream sdt(sdtPath, std::ios::binary);
    if (!sdt) {
        error = "cannot open sound bank: " + sdtPath;
        return false;
    }
    std::vector<uint8_t> index((std::istreambuf_iterator<char>(sdt)),
                               std::istreambuf_iterator<char>());
    if (index.empty() || index.size() % 12 != 0) {
        error = "invalid SDT sound index: " + sdtPath;
        return false;
    }

    std::string rawPath = sdtPath;
    const size_t dot = rawPath.find_last_of('.');
    if (dot != std::string::npos) rawPath = rawPath.substr(0, dot);
    rawPath += ".RAW";
    std::ifstream raw(rawPath, std::ios::binary);
    if (!raw) {
        error = "cannot open sound data: " + rawPath;
        return false;
    }
    std::vector<uint8_t> samples((std::istreambuf_iterator<char>(raw)),
                                 std::istreambuf_iterator<char>());
    if (samples.empty()) {
        error = "empty sound data: " + rawPath;
        return false;
    }

    std::vector<SfxEntry> entries;
    entries.reserve(index.size() / 12);
    for (size_t i = 0; i + 12 <= index.size(); i += 12) {
        SfxEntry entry;
        std::memcpy(&entry.start, index.data() + i, 4);
        std::memcpy(&entry.size, index.data() + i + 4, 4);
        std::memcpy(&entry.rate, index.data() + i + 8, 4);
        if (entry.rate == 0) entry.rate = 11025;
        if (entry.start + entry.size > samples.size()) continue;
        entries.push_back(entry);
    }
    if (entries.empty()) {
        error = "sound bank has no valid samples";
        return false;
    }

    if (device_) SDL_LockAudioDevice(device_);
    sfxRaw_ = std::move(samples);
    sfxEntries_ = std::move(entries);
    for (SfxVoice& voice : sfxVoices_) voice = SfxVoice{};
    if (device_) SDL_UnlockAudioDevice(device_);
    return true;
}

void AudioManager::playSfx(int id, float volume) {
    if (!device_ || sfxRaw_.empty() || id < 0 ||
        id >= static_cast<int>(sfxEntries_.size())) {
        return;
    }
    SDL_LockAudioDevice(device_);
    SfxVoice* target = nullptr;
    for (SfxVoice& voice : sfxVoices_) {
        if (voice.id < 0) {
            target = &voice;
            break;
        }
    }
    if (!target) target = &sfxVoices_[0];
    target->id = id;
    target->position = 0.0;
    target->volume = std::clamp(volume, 0.0f, 1.0f);
    SDL_UnlockAudioDevice(device_);
}

void AudioManager::playUiClick() {
    if (sfxLoaded()) playSfx(12, 0.35f);
}

void AudioManager::playCrashSound() {
    if (sfxLoaded()) playSfx(14, 0.65f);
}

void AudioManager::audioCallback(void* userdata, Uint8* stream, int length) {
    auto* self = static_cast<AudioManager*>(userdata);
    const int samples = length / static_cast<int>(sizeof(Sint16)) / kChannels;
    self->mix(reinterpret_cast<Sint16*>(stream), samples);
}

void AudioManager::mix(Sint16* out, int samples) {
    if (!out || samples <= 0) return;

    const bool paused = paused_.load();
    const float master = volume_.load();
    const float speed = speed_.load();
    const float throttle = throttle_.load();
    const float crash = paused ? 0.0f : crash_.load();
    // Music keeps playing while paused (pause menus), the engine does not.
    const float engineVolume =
        (paused ? 0.0f : engineVolume_.load()) * master;
    const float musicVolume =
        musicEnabled_.load() ? musicVolume_.load() * master : 0.0f;

    const double sampleRate = spec_.freq > 0 ? spec_.freq : kSampleRate;
    const float speedFactor = std::min(1.0f, speed / 18.0f);
    const float throttlePos = std::max(0.0f, throttle);
    const double baseFreq = 27.0 + speedFactor * 58.0 + throttlePos * 9.0;

    const size_t musicFrames =
        musicChannels_ > 0 ? musicSamples_.size() / musicChannels_ : 0;
    const bool playMusic = musicVolume > 0.0001f && musicFrames > 1;
    const double musicStep =
        playMusic ? static_cast<double>(musicRate_) / sampleRate : 0.0;

    if (!paused && pickupRequests_.exchange(0) > 0) {
        pickupEnvelope_ = 1.0f;
        pickupPhase_ = 0.0;
    }

    for (int i = 0; i < samples; ++i) {
        phase_ += baseFreq / sampleRate;
        if (phase_ >= 1.0) phase_ -= 1.0;

        const float fundamental = std::sin(
            static_cast<float>(phase_) * kTwoPi);
        const float harmonic2 = std::sin(
            static_cast<float>(phase_) * kTwoPi * 2.0f);
        const float harmonic3 = std::sin(
            static_cast<float>(phase_) * kTwoPi * 3.0f);

        noiseState_ = noiseState_ * 1664525u + 1013904223u;
        const float noise =
            (static_cast<float>((noiseState_ >> 8) & 0xffff) / 32768.0f - 1.0f) *
            (0.008f + speedFactor * 0.02f);

        float engine = (fundamental * 0.72f + harmonic2 * 0.18f +
                        harmonic3 * 0.06f + noise) *
                       (0.055f + speedFactor * 0.20f + throttlePos * 0.045f) *
                       engineVolume;
        if (crash > 0.0f) {
            noiseState_ = noiseState_ * 1664525u + 1013904223u;
            engine += (static_cast<float>((noiseState_ >> 8) & 0xffff) / 32768.0f -
                       1.0f) *
                      0.09f * crash * master;
        }
        lowpass_ += (engine - lowpass_) * 0.075f;

        // Street ambience: a low traffic rumble bed plus slow-gusting wind.
        // Generated from the same noise source, low-passed to two bands.
        float amb = 0.0f;
        const float ambientLevel = (paused ? 0.0f : ambient_.load()) * master;
        if (ambientLevel > 0.0001f) {
            noiseState_ = noiseState_ * 1664525u + 1013904223u;
            const float white =
                static_cast<float>((noiseState_ >> 8) & 0xffff) / 32768.0f -
                1.0f;
            ambRumble_ += (white - ambRumble_) * 0.0018f;  // deep rumble
            ambAir_ += (white - ambAir_) * 0.03f;          // wind in air
            ambWindPhase_ += 0.13 / sampleRate;
            if (ambWindPhase_ >= 1.0) ambWindPhase_ -= 1.0;
            const float gust =
                0.5f + 0.5f * std::sin(static_cast<float>(ambWindPhase_) *
                                       kTwoPi);
            amb = (ambRumble_ * 0.85f + ambAir_ * (0.12f + gust * 0.30f)) *
                  0.7f * ambientLevel;
        } else {
            ambRumble_ *= 0.98f;
            ambAir_ *= 0.98f;
        }

        float pickup = 0.0f;
        if (!paused && pickupEnvelope_ > 0.0f) {
            pickupPhase_ += 920.0 / sampleRate;
            if (pickupPhase_ >= 1.0) pickupPhase_ -= 1.0;
            pickup = std::sin(static_cast<float>(pickupPhase_) * kTwoPi) *
                     0.22f * pickupEnvelope_ * master;
            const float decay =
                1.0f / static_cast<float>(sampleRate * 0.28);
            pickupEnvelope_ = std::max(0.0f, pickupEnvelope_ - decay);
        }

        float sfx = 0.0f;
        for (SfxVoice& voice : sfxVoices_) {
            if (voice.id < 0) continue;
            if (voice.id >= static_cast<int>(sfxEntries_.size())) {
                voice.id = -1;
                continue;
            }
            const SfxEntry& entry = sfxEntries_[static_cast<size_t>(voice.id)];
            if (voice.position >= static_cast<double>(entry.size)) {
                voice.id = -1;
                continue;
            }
            const size_t index = static_cast<size_t>(voice.position);
            const int sample = static_cast<int>(sfxRaw_[entry.start + index]);
            sfx += ((static_cast<float>(sample) - 128.0f) / 128.0f) *
                   voice.volume;
            voice.position +=
                static_cast<double>(entry.rate) / sampleRate;
        }
        sfx *= master;

        if (playMusic) {
            const size_t frame = static_cast<size_t>(musicPosition_);
            if (frame + 1 < musicFrames) {
                const double fraction = musicPosition_ - static_cast<double>(frame);
                for (int c = 0; c < kChannels; ++c) {
                    const int sourceChannel =
                        musicChannels_ == 1 ? 0 : std::min(c, musicChannels_ - 1);
                    const float s0 = static_cast<float>(musicSamples_[
                        frame * musicChannels_ + sourceChannel]);
                    const float s1 = static_cast<float>(musicSamples_[
                        (frame + 1) * musicChannels_ + sourceChannel]);
                    const float value =
                        (s0 + (s1 - s0) * static_cast<float>(fraction)) / 32768.0f;
                    const float sample =
                        lowpass_ + value * musicVolume + pickup + sfx + amb;
                    out[i * kChannels + c] = static_cast<Sint16>(
                        std::clamp(sample, -1.0f, 1.0f) * 32767.0f);
                }
                musicPosition_ += musicStep;
                if (musicPosition_ >= static_cast<double>(musicFrames - 1)) {
                    musicPosition_ = 0.0; // loop the track
                }
                continue;
            }
            musicPosition_ = 0.0;
        }

        const float total = lowpass_ + amb + pickup + sfx;
        const Sint16 s =
            static_cast<Sint16>(std::clamp(total, -1.0f, 1.0f) * 32767.0f);
        out[i * kChannels + 0] = s;
        out[i * kChannels + 1] = s;
    }

    if (crash > 0.0f) crash_.store(std::max(0.0f, crash - 0.02f));
}

} // namespace gta
