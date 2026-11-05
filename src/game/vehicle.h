#pragma once

#include "core/math3d.h"
#include "game/world.h"

namespace gta {

struct VehicleControls {
    float throttle = 0.0f; // -1 brake/reverse, +1 accelerate
    float steer = 0.0f;    // -1 left, +1 right
    bool handbrake = false;
};

class Vehicle {
public:
    void reset(const Vec3& position);
    void setYaw(float yaw) { yaw_ = yaw; }
    void update(float dt, const VehicleControls& controls, const World& world);

    const Vec3& position() const { return pos_; }
    float yaw() const { return yaw_; }
    float speed() const { return speed_; }
    float steering() const { return steer_; }
    float bob() const { return bob_; }
    bool crashed() const { return crashFlash_ > 0.0f; }

    void setSprite(int spriteIndex, int pixelWidth, int pixelHeight, int model);
    int spriteIndex() const { return spriteIndex_; }
    float spriteWidth() const { return spriteW_; }
    float spriteHeight() const { return spriteH_; }
    int model() const { return model_; }

private:
    Vec3 pos_{128.5f, 1.0f, 128.5f};
    float yaw_ = 0.0f;
    float speed_ = 0.0f;
    float steer_ = 0.0f;
    float bob_ = 0.0f;
    float crashFlash_ = 0.0f;

    int spriteIndex_ = 0;
    int model_ = 0;
    float spriteW_ = 1.0f;
    float spriteH_ = 1.0f;
};

} // namespace gta
