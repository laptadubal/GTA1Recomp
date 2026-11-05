#include "input/input_manager.h"

#include <algorithm>
#include <cmath>

namespace gta {

namespace {

float axisNorm(Sint16 value) {
    return std::clamp(static_cast<float>(value) / 32767.0f, -1.0f, 1.0f);
}

float triggerNorm(Sint16 value) {
    return std::clamp(static_cast<float>(value) / 32767.0f, 0.0f, 1.0f);
}

} // namespace

InputManager::~InputManager() { shutdown(); }

bool InputManager::init(std::string& error) {
    shutdown();
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
        error = std::string("SDL controller initialisation failed: ") +
                SDL_GetError();
        return false;
    }

    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) openController(i);
    }
    SDL_GameControllerEventState(SDL_ENABLE);
    return true;
}

void InputManager::shutdown() {
    for (SDL_GameController* controller : controllers_) {
        if (controller) SDL_GameControllerClose(controller);
    }
    controllers_.clear();
}

void InputManager::openController(int deviceIndex) {
    if (!SDL_IsGameController(deviceIndex)) return;
    if (controllers_.size() >= 4) return;
    if (SDL_GameController* controller = SDL_GameControllerOpen(deviceIndex)) {
        controllers_.push_back(controller);
    }
}

void InputManager::closeController(SDL_JoystickID id) {
    for (auto it = controllers_.begin(); it != controllers_.end(); ++it) {
        SDL_GameController* controller = *it;
        SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
        if (SDL_JoystickInstanceID(joystick) == id) {
            SDL_GameControllerClose(controller);
            controllers_.erase(it);
            return;
        }
    }
}

std::string InputManager::gamepadName() const {
    if (controllers_.empty()) return {};
    const char* name = SDL_GameControllerName(controllers_.front());
    return name ? name : "Gamepad";
}

void InputManager::handleEvent(const SDL_Event& event) {
    switch (event.type) {
    case SDL_QUIT:
        quit_ = true;
        break;
    case SDL_KEYDOWN:
        if (event.key.repeat) break;
        if (event.key.keysym.scancode < SDL_NUM_SCANCODES) {
            keys_[event.key.keysym.scancode] = true;
        }
        break;
    case SDL_KEYUP:
        if (event.key.keysym.scancode < SDL_NUM_SCANCODES) {
            keys_[event.key.keysym.scancode] = false;
        }
        break;
    case SDL_MOUSEMOTION:
        mouseX_ = event.motion.x;
        mouseY_ = event.motion.y;
        break;
    case SDL_MOUSEBUTTONDOWN:
        if (event.button.button == SDL_BUTTON_LEFT) mouseDown_ = true;
        mouseX_ = event.button.x;
        mouseY_ = event.button.y;
        break;
    case SDL_MOUSEBUTTONUP:
        if (event.button.button == SDL_BUTTON_LEFT) mouseDown_ = false;
        break;
    case SDL_CONTROLLERDEVICEADDED:
        openController(event.cdevice.which);
        break;
    case SDL_CONTROLLERDEVICEREMOVED:
        closeController(event.cdevice.which);
        break;
    case SDL_FINGERDOWN:
        touchDown(event.tfinger.fingerId, event.tfinger.x, event.tfinger.y);
        break;
    case SDL_FINGERMOTION:
        touchMotion(event.tfinger.fingerId, event.tfinger.x, event.tfinger.y);
        break;
    case SDL_FINGERUP:
        touchUp(event.tfinger.fingerId);
        break;
    default:
        break;
    }
}

void InputManager::touchDown(Sint64 id, float x, float y) {
    if (x < 0.5f && steerFinger_ < 0) {
        steerFinger_ = id;
        steerOriginX_ = x;
        touchSteer_ = 0.0f;
    } else if (x >= 0.5f && throttleFinger_ < 0) {
        throttleFinger_ = id;
        touchThrottle_ = y >= 0.5f ? 1.0f : -1.0f;
    }
}

void InputManager::touchMotion(Sint64 id, float x, float y) {
    if (id == steerFinger_) {
        touchSteer_ = std::clamp((x - steerOriginX_) / 0.12f, -1.0f, 1.0f);
    } else if (id == throttleFinger_) {
        touchThrottle_ = y >= 0.5f ? 1.0f : -1.0f;
    }
}

void InputManager::touchUp(Sint64 id) {
    if (id == steerFinger_) {
        steerFinger_ = -1;
        touchSteer_ = 0.0f;
    } else if (id == throttleFinger_) {
        throttleFinger_ = -1;
        touchThrottle_ = 0.0f;
    }
}

void InputManager::newFrame() {
    mousePressed_ = mouseDown_ && !previousMouseDown_;
    mouseReleased_ = !mouseDown_ && previousMouseDown_;
    previousMouseDown_ = mouseDown_;

    for (int i = 0; i < static_cast<int>(GameAction::Count); ++i) {
        previousActions_[i] = actions_[i];
        actions_[i] = 0.0f;
    }
    updateActions();
}

void InputManager::updateActions() {
    auto key = [&](SDL_Scancode scancode) { return keys_[scancode]; };
    auto set = [&](GameAction action, float value) {
        actions_[static_cast<int>(action)] =
            std::max(actions_[static_cast<int>(action)], value);
    };

    if (key(SDL_SCANCODE_W) || key(SDL_SCANCODE_UP)) set(GameAction::Accelerate, 1.0f);
    if (key(SDL_SCANCODE_S) || key(SDL_SCANCODE_DOWN)) set(GameAction::Brake, 1.0f);
    if (key(SDL_SCANCODE_A) || key(SDL_SCANCODE_LEFT)) set(GameAction::SteerLeft, 1.0f);
    if (key(SDL_SCANCODE_D) || key(SDL_SCANCODE_RIGHT)) set(GameAction::SteerRight, 1.0f);
    if (key(SDL_SCANCODE_SPACE)) set(GameAction::Handbrake, 1.0f);
    if (key(SDL_SCANCODE_P)) set(GameAction::TogglePause, 1.0f);
    if (key(SDL_SCANCODE_F1)) set(GameAction::ToggleFullscreen, 1.0f);
    if (key(SDL_SCANCODE_F2)) set(GameAction::ToggleFxaa, 1.0f);
    if (key(SDL_SCANCODE_F3)) set(GameAction::ToggleDebug, 1.0f);
    if (key(SDL_SCANCODE_F4)) set(GameAction::ToggleMinimap, 1.0f);
    if (key(SDL_SCANCODE_C)) set(GameAction::ToggleCamera, 1.0f);
    if (key(SDL_SCANCODE_R)) set(GameAction::Reset, 1.0f);
    if (key(SDL_SCANCODE_F) || key(SDL_SCANCODE_E)) set(GameAction::EnterVehicle, 1.0f);
    if (key(SDL_SCANCODE_LCTRL) || key(SDL_SCANCODE_RCTRL) || mouseDown_)
        set(GameAction::Attack, 1.0f);
    if (key(SDL_SCANCODE_TAB) || key(SDL_SCANCODE_Q))
        set(GameAction::NextWeapon, 1.0f);
    if (key(SDL_SCANCODE_F12)) set(GameAction::Screenshot, 1.0f);
    if (key(SDL_SCANCODE_EQUALS) || key(SDL_SCANCODE_KP_PLUS)) set(GameAction::ZoomIn, 1.0f);
    if (key(SDL_SCANCODE_MINUS) || key(SDL_SCANCODE_KP_MINUS)) set(GameAction::ZoomOut, 1.0f);
    if (key(SDL_SCANCODE_M)) set(GameAction::MusicNext, 1.0f);

    // Menu navigation (also mirrors WASD; menus are only active outside play).
    if (key(SDL_SCANCODE_W) || key(SDL_SCANCODE_UP)) set(GameAction::MenuUp, 1.0f);
    if (key(SDL_SCANCODE_S) || key(SDL_SCANCODE_DOWN)) set(GameAction::MenuDown, 1.0f);
    if (key(SDL_SCANCODE_A) || key(SDL_SCANCODE_LEFT)) set(GameAction::MenuLeft, 1.0f);
    if (key(SDL_SCANCODE_D) || key(SDL_SCANCODE_RIGHT)) set(GameAction::MenuRight, 1.0f);
    if (key(SDL_SCANCODE_RETURN) || key(SDL_SCANCODE_KP_ENTER) ||
        key(SDL_SCANCODE_SPACE)) {
        set(GameAction::MenuSelect, 1.0f);
    }
    if (key(SDL_SCANCODE_ESCAPE) || key(SDL_SCANCODE_BACKSPACE)) {
        set(GameAction::MenuBack, 1.0f);
    }

    if (touchSteer_ < -0.05f) set(GameAction::SteerLeft, -touchSteer_);
    if (touchSteer_ > 0.05f) set(GameAction::SteerRight, touchSteer_);
    if (touchThrottle_ > 0.05f) set(GameAction::Accelerate, touchThrottle_);
    if (touchThrottle_ < -0.05f) set(GameAction::Brake, -touchThrottle_);

    if (!controllers_.empty()) {
        SDL_GameController* controller = controllers_.front();
        const float leftX = axisNorm(SDL_GameControllerGetAxis(
            controller, SDL_CONTROLLER_AXIS_LEFTX));
        if (leftX < -0.22f) set(GameAction::SteerLeft, -leftX);
        if (leftX > 0.22f) set(GameAction::SteerRight, leftX);

        const float rightTrigger = triggerNorm(SDL_GameControllerGetAxis(
            controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));
        const float leftTrigger = triggerNorm(SDL_GameControllerGetAxis(
            controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
        if (rightTrigger > 0.05f) set(GameAction::Accelerate, rightTrigger);
        if (leftTrigger > 0.05f) set(GameAction::Brake, leftTrigger);

        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER))
            set(GameAction::Handbrake, 1.0f);
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))
            set(GameAction::EnterVehicle, 1.0f);
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START))
            set(GameAction::TogglePause, 1.0f);
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_Y))
            set(GameAction::NextWeapon, 1.0f);
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_X))
            set(GameAction::Attack, 1.0f);
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSTICK))
            set(GameAction::Reset, 1.0f);
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSTICK))
            set(GameAction::ToggleCamera, 1.0f);
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK))
            set(GameAction::ToggleDebug, 1.0f);
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP))
            set(GameAction::MenuUp, 1.0f);
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN))
            set(GameAction::MenuDown, 1.0f);
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT))
            set(GameAction::MenuLeft, 1.0f);
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))
            set(GameAction::MenuRight, 1.0f);
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A))
            set(GameAction::MenuSelect, 1.0f);
        if (SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_B))
            set(GameAction::MenuBack, 1.0f);
    }
}

bool InputManager::down(GameAction action) const {
    return actions_[static_cast<int>(action)] >= 0.5f;
}

bool InputManager::pressed(GameAction action) const {
    const int i = static_cast<int>(action);
    return actions_[i] >= 0.5f && previousActions_[i] < 0.5f;
}

float InputManager::value(GameAction action) const {
    return actions_[static_cast<int>(action)];
}

float InputManager::throttle() const {
    return value(GameAction::Accelerate) - value(GameAction::Brake);
}

float InputManager::steer() const {
    return value(GameAction::SteerRight) - value(GameAction::SteerLeft);
}

bool InputManager::handbrake() const { return down(GameAction::Handbrake); }

void InputManager::rumble(float strength, int durationMs) {
    if (controllers_.empty()) return;
    SDL_GameControllerRumble(controllers_.front(),
                             static_cast<Uint16>(std::clamp(strength, 0.0f, 1.0f) * 65535),
                             static_cast<Uint16>(std::clamp(strength, 0.0f, 1.0f) * 65535),
                             static_cast<Uint32>(durationMs));
}

} // namespace gta
