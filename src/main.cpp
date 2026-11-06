#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <SDL.h>

#include "audio/audio_manager.h"
#include "core/config.h"
#include "core/game.h"
#include "graphics/renderer.h"
#include "input/input_manager.h"

namespace {

void printUsage(const char* program) {
    std::printf(
        "Grand Theft Auto (DOS) reimplementation\n"
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "  --data PATH         Directory containing NYC.CMP and STYLE001.GRY\n"
        "  --city NAME         City to load (default: NYC)\n"
        "  --width N           Window width (default: 1280)\n"
        "  --height N          Window height (default: 720)\n"
        "  --fullscreen        Start fullscreen\n"
        "  --windowed          Start in a window\n"
        "  --vsync / --no-vsync\n"
        "  --msaa N            Multisample count: 0, 2, 4, 8 or 16\n"
        "  --fxaa / --no-fxaa  Toggle the FXAA post-process\n"
        "  --scale F           Render scale (0.25 - 2.0)\n"
        "  --volume F          Master volume (0 - 1)\n"
        "  --car N             Car model index\n"
        "  --debug             Show debug information\n"
        "  --demo              Drive automatically (smoke test / capture)\n"
        "  --spawn X,Y         Start on the street nearest this map cell\n"
        "  --screen NAME       Start on intro, menu, settings, controls, game,\n"
        "                      pause or mission (development helper)\n"
        "  --config PATH       Configuration file (default: user pref dir)\n"
        "  --screenshot PATH   Render a few frames, save a BMP and exit\n"
        "  --frames N          Frames to render before the screenshot (default: 8)\n"
        "  --fixed-dt SECONDS  Use a fixed simulation step (testing helper)\n"
        "  --help              Show this help\n",
        program);
}

bool nextValue(int argc, char** argv, int& i, std::string& out) {
    if (i + 1 >= argc) return false;
    out = argv[++i];
    return true;
}

std::string prefScreenshotPath(int index) {
    std::string base;
    if (char* pref = SDL_GetPrefPath("dos_gta_recomp", "dos_gta_recomp")) {
        base = pref;
        SDL_free(pref);
    }
    char name[64];
    std::snprintf(name, sizeof(name), "screenshot_%03d.bmp", index);
    return base + name;
}

} // namespace

int main(int argc, char** argv) {
    gta::GameConfig config = gta::defaultConfig();
    std::string configPath;
    std::string screenshotPath;
    std::string startScreen;
    int screenshotFrames = 8;
    float fixedDt = 0.0f;
    bool screenshotMode = false;
    bool demoMode = false;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER |
                 SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "SDL initialisation failed: %s\n", SDL_GetError());
        return 1;
    }

    configPath = gta::defaultConfigPath();
    if (!configPath.empty()) {
        std::string ignored;
        gta::loadConfig(configPath, config, ignored);
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        std::string value;
        if (arg.rfind("-psn_", 0) == 0) {
            // LaunchServices may pass a process serial number to app bundles.
            continue;
        }
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            SDL_Quit();
            return 0;
        } else if (arg == "--data") {
            if (!nextValue(argc, argv, i, value)) {
                std::fprintf(stderr, "--data needs a path\n");
                SDL_Quit();
                return 1;
            }
            config.dataDir = value;
        } else if (arg == "--city") {
            if (!nextValue(argc, argv, i, value)) return 1;
            config.city = value;
        } else if (arg == "--width") {
            if (!nextValue(argc, argv, i, value)) return 1;
            config.graphics.width = std::atoi(value.c_str());
        } else if (arg == "--height") {
            if (!nextValue(argc, argv, i, value)) return 1;
            config.graphics.height = std::atoi(value.c_str());
        } else if (arg == "--fullscreen") {
            config.graphics.fullscreen = true;
        } else if (arg == "--windowed") {
            config.graphics.fullscreen = false;
        } else if (arg == "--vsync") {
            config.graphics.vsync = true;
        } else if (arg == "--no-vsync") {
            config.graphics.vsync = false;
        } else if (arg == "--msaa") {
            if (!nextValue(argc, argv, i, value)) return 1;
            config.graphics.msaa = std::atoi(value.c_str());
        } else if (arg == "--fxaa") {
            config.graphics.fxaa = true;
        } else if (arg == "--no-fxaa") {
            config.graphics.fxaa = false;
        } else if (arg == "--scale") {
            if (!nextValue(argc, argv, i, value)) return 1;
            config.graphics.renderScale = std::stof(value);
        } else if (arg == "--volume") {
            if (!nextValue(argc, argv, i, value)) return 1;
            config.audio.masterVolume = std::stof(value);
        } else if (arg == "--car") {
            if (!nextValue(argc, argv, i, value)) return 1;
            config.carIndex = std::atoi(value.c_str());
        } else if (arg == "--debug") {
            config.showDebug = true;
        } else if (arg == "--demo") {
            demoMode = true;
        } else if (arg == "--spawn") {
            if (!nextValue(argc, argv, i, value)) return 1;
            int sx = -1, sy = -1;
            if (std::sscanf(value.c_str(), "%d,%d", &sx, &sy) != 2) {
                std::fprintf(stderr, "--spawn needs X,Y\n");
                SDL_Quit();
                return 1;
            }
            config.spawnX = sx;
            config.spawnY = sy;
        } else if (arg == "--screen") {
            if (!nextValue(argc, argv, i, startScreen)) return 1;
        } else if (arg == "--config") {
            if (!nextValue(argc, argv, i, configPath)) return 1;
            std::string ignored;
            gta::loadConfig(configPath, config, ignored);
        } else if (arg == "--screenshot") {
            if (!nextValue(argc, argv, i, screenshotPath)) return 1;
            screenshotMode = true;
        } else if (arg == "--frames") {
            if (!nextValue(argc, argv, i, value)) return 1;
            screenshotFrames = std::max(1, std::atoi(value.c_str()));
        } else if (arg == "--fixed-dt") {
            if (!nextValue(argc, argv, i, value)) return 1;
            fixedDt = std::max(0.0f, std::stof(value));
        } else {
            std::fprintf(stderr, "unknown option: %s (use --help)\n", arg.c_str());
            SDL_Quit();
            return 1;
        }
    }

    config.graphics.width = std::max(320, config.graphics.width);
    config.graphics.height = std::max(200, config.graphics.height);
    config.graphics.msaa = std::clamp(config.graphics.msaa, 0, 16);

    gta::Renderer renderer;
    std::string error;
    if (!renderer.create(config.graphics, "Grand Theft Auto - DOS Recompilation",
                         error)) {
        std::fprintf(stderr, "renderer: %s\n", error.c_str());
        SDL_Quit();
        return 1;
    }

    gta::InputManager input;
    if (!input.init(error)) {
        std::fprintf(stderr, "warning: %s\n", error.c_str());
    }

    gta::AudioManager audio;
    if (!audio.init(config.audio, error)) {
        std::fprintf(stderr, "warning: %s\n", error.c_str());
    }
    audio.setMasterVolume(config.audio.masterVolume);

    gta::Game game;
    if (!game.init(renderer, config, error)) {
        std::fprintf(stderr, "game: %s\n", error.c_str());
        game.shutdown();
        audio.shutdown();
        input.shutdown();
        renderer.destroy();
        SDL_Quit();
        return 1;
    }

    {
        std::string sfxError;
        const std::string sfxPath =
            game.assets().dataDir() + "/AUDIO/LEVEL001.SDT";
        if (!audio.loadSfxBank(sfxPath, sfxError)) {
            std::fprintf(stderr, "warning: %s\n", sfxError.c_str());
        }
    }

    game.setDemo(demoMode);
    if (!startScreen.empty() && !game.showScreen(startScreen)) {
        std::fprintf(stderr, "unknown screen: %s\n", startScreen.c_str());
        game.shutdown();
        audio.shutdown();
        input.shutdown();
        renderer.destroy();
        SDL_Quit();
        return 1;
    }

    std::printf("Loaded %s from %s\n", game.assets().cityName().c_str(),
                game.assets().dataDir().c_str());
    std::printf("Window %dx%d, MSAA %d, FXAA %s, render scale %.2f\n",
                renderer.windowWidth(), renderer.windowHeight(), renderer.msaa(),
                renderer.fxaaEnabled() ? "on" : "off", renderer.renderScale());
    if (!input.gamepadConnected()) {
        std::printf("No gamepad detected - keyboard controls active.\n");
    } else {
        std::printf("Gamepad: %s\n", input.gamepadName().c_str());
    }

    Uint64 previous = SDL_GetPerformanceCounter();
    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    int frame = 0;
    int screenshotIndex = 0;
    bool running = true;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_WINDOWEVENT &&
                (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                 event.window.event == SDL_WINDOWEVENT_RESIZED)) {
                renderer.resize(0, 0);
            }
            input.handleEvent(event);
        }

        const Uint64 now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>((now - previous) / frequency);
        previous = now;
        dt = std::clamp(dt, 0.0f, 0.1f);
        if (fixedDt > 0.0f) dt = fixedDt;

        input.newFrame();
        game.update(dt, input, audio);
        game.render(renderer);

        if (game.screenshotRequested()) {
            game.clearScreenshotRequest();
            const std::string path = prefScreenshotPath(++screenshotIndex);
            if (renderer.saveScreenshot(path, error)) {
                std::printf("Saved %s\n", path.c_str());
            } else {
                std::fprintf(stderr, "screenshot: %s\n", error.c_str());
            }
        }

        ++frame;
        if (screenshotMode && frame >= screenshotFrames) {
            std::string target =
                screenshotPath.empty() ? prefScreenshotPath(0) : screenshotPath;
            glFinish();
            bool saved = renderer.saveScreenshot(target, error);
            if (!saved) {
                const std::string fallback = prefScreenshotPath(0);
                if (target != fallback) {
                    saved = renderer.saveScreenshot(fallback, error);
                    if (saved) target = fallback;
                }
            }
            if (saved) {
                std::printf("Saved %s\n", target.c_str());
            } else {
                std::fprintf(stderr, "screenshot: %s\n", error.c_str());
            }
            running = false;
        }
        if (input.quitRequested() || game.quitRequested()) running = false;
    }

    game.saveProgressIfActive();
    game.config().graphics.fullscreen = renderer.fullscreen();
    SDL_GetWindowSize(renderer.window(), &game.config().graphics.width,
                      &game.config().graphics.height);
    if (!configPath.empty() && !screenshotMode) {
        std::string saveError;
        if (!gta::saveConfig(configPath, game.config(), saveError)) {
            std::fprintf(stderr, "config: %s\n", saveError.c_str());
        }
    }

    game.shutdown();
    audio.shutdown();
    input.shutdown();
    renderer.destroy();
    SDL_Quit();
    return 0;
}
