#pragma once

#include <string>
#include <vector>

#include <SDL.h>

namespace gta {

enum class GameAction {
    Accelerate,
    Brake,
    SteerLeft,
    SteerRight,
    Handbrake,
    TogglePause,
    ToggleFullscreen,
    ToggleFxaa,
    ToggleDebug,
    ToggleMinimap,
    ToggleCamera,
    Reset,
    EnterVehicle,
    Attack,
    NextWeapon,
    Screenshot,
    ZoomIn,
    ZoomOut,
    MusicNext,
    MenuUp,
    MenuDown,
    MenuLeft,
    MenuRight,
    MenuSelect,
    MenuBack,
    Quit,
    Count
};

class InputManager {
public:
    InputManager() = default;
    ~InputManager();
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    bool init(std::string& error);
    void shutdown();

    void handleEvent(const SDL_Event& event);
    // Recomputes action states.  Call once per frame after polling events.
    void newFrame();

    bool down(GameAction action) const;
    bool pressed(GameAction action) const;
    float value(GameAction action) const;

    float throttle() const;
    float steer() const;
    bool handbrake() const;

    bool quitRequested() const { return quit_; }
    void requestQuit() { quit_ = true; }

    int mouseX() const { return mouseX_; }
    int mouseY() const { return mouseY_; }
    bool mouseDown() const { return mouseDown_; }
    bool mousePressed() const { return mousePressed_; }
    bool mouseReleased() const { return mouseReleased_; }

    bool gamepadConnected() const { return !controllers_.empty(); }
    std::string gamepadName() const;

    void rumble(float strength, int durationMs);

private:
    void openController(int deviceIndex);
    void closeController(SDL_JoystickID id);
    void updateActions();
    void touchDown(Sint64 id, float x, float y);
    void touchMotion(Sint64 id, float x, float y);
    void touchUp(Sint64 id);

    bool keys_[SDL_NUM_SCANCODES] = {};
    bool quit_ = false;
    int mouseX_ = 0;
    int mouseY_ = 0;
    bool mouseDown_ = false;
    bool previousMouseDown_ = false;
    bool mousePressed_ = false;
    bool mouseReleased_ = false;

    std::vector<SDL_GameController*> controllers_;
    float actions_[static_cast<int>(GameAction::Count)] = {};
    float previousActions_[static_cast<int>(GameAction::Count)] = {};

    // Simple touch layout (used on Android): the left half is a virtual
    // steering stick, the right half is accelerate (lower) / brake (upper).
    Sint64 steerFinger_ = -1;
    Sint64 throttleFinger_ = -1;
    float steerOriginX_ = 0.0f;
    float touchSteer_ = 0.0f;
    float touchThrottle_ = 0.0f;
};

} // namespace gta
