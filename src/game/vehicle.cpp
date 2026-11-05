#include "game/vehicle.h"

#include <algorithm>
#include <cmath>

namespace gta {

namespace {
constexpr float kMaxSpeed = 18.0f;
constexpr float kMaxReverse = 6.0f;
constexpr float kAcceleration = 13.0f;
constexpr float kBrake = 24.0f;
constexpr float kDrag = 0.65f;
constexpr float kTurnRate = 2.6f;
constexpr float kRadius = 0.26f;
} // namespace

void Vehicle::reset(const Vec3& position) {
    pos_ = position;
    yaw_ = 0.0f;
    speed_ = 0.0f;
    steer_ = 0.0f;
    crashFlash_ = 0.0f;
}

void Vehicle::setSprite(int spriteIndex, int pixelWidth, int pixelHeight, int model) {
    spriteIndex_ = spriteIndex;
    model_ = model;
    spriteW_ = std::max(0.25f, pixelWidth / 64.0f);
    spriteH_ = std::max(0.25f, pixelHeight / 64.0f);
}

void Vehicle::update(float dt, const VehicleControls& controls, const World& world) {
    dt = std::clamp(dt, 0.0f, 0.05f);

    const float throttle = std::clamp(controls.throttle, -1.0f, 1.0f);
    const float steerTarget = std::clamp(controls.steer, -1.0f, 1.0f);
    steer_ += (steerTarget - steer_) * std::min(1.0f, dt * 12.0f);

    if (throttle > 0.01f) {
        speed_ += kAcceleration * throttle * dt;
    } else if (throttle < -0.01f) {
        if (speed_ > 0.3f) {
            speed_ += kBrake * throttle * dt; // braking
        } else {
            speed_ += kAcceleration * 0.55f * throttle * dt; // reverse
        }
    }

    // Rolling drag + handbrake.
    const float drag = controls.handbrake ? 3.2f : kDrag;
    const float sign = speed_ > 0.0f ? 1.0f : (speed_ < 0.0f ? -1.0f : 0.0f);
    speed_ -= sign * drag * dt;
    if (std::abs(speed_) < 0.06f) speed_ = 0.0f;
    speed_ = std::clamp(speed_, -kMaxReverse, kMaxSpeed);

    // Steering works from a crawl (so tight streets are drivable) and gets
    // more responsive with speed.
    const float speedFactor =
        0.22f + 0.78f * std::min(1.0f, std::abs(speed_) / 5.0f);
    const float dir = speed_ >= 0.0f ? 1.0f : -1.0f;
    const float turnBoost = controls.handbrake ? 1.45f : 1.0f;
    yaw_ += steer_ * kTurnRate * speedFactor * dir * turnBoost * dt;

    // Keep yaw in a friendly range.
    while (yaw_ > kPi) yaw_ -= 2.0f * kPi;
    while (yaw_ < -kPi) yaw_ += 2.0f * kPi;

    const float fx = std::sin(yaw_);
    const float fz = -std::cos(yaw_);
    Vec3 next = pos_;
    next.x += fx * speed_ * dt;
    next.z += fz * speed_ * dt;

    // Collision: probe the car's four corners against drivable cells.
    const int currentSurface =
        world.roadSurface(static_cast<int>(std::floor(pos_.x)),
                          static_cast<int>(std::floor(pos_.z)));

    auto surfaceAt = [&](const Vec3& p) {
        const int cx = static_cast<int>(std::floor(p.x));
        const int cz = static_cast<int>(std::floor(p.z));
        const int road = world.roadSurface(cx, cz);
        return road > 0 ? road : world.surfaceHeight(cx, cz);
    };
    auto canMoveTo = [&](const Vec3& candidate) {
        if (!world.isPassable(candidate.x, candidate.z)) return false;
        const int surface = surfaceAt(candidate);
        if (currentSurface > 0 && std::abs(surface - currentSurface) > 1)
            return false;
        const float sx = std::cos(yaw_) * kRadius;
        const float sz = std::sin(yaw_) * kRadius;
        const float px = fx * kRadius * 1.6f;
        const float pz = fz * kRadius * 1.6f;
        const Vec2 corners[4] = {
            {candidate.x + px + sx, candidate.z + pz + sz},
            {candidate.x + px - sx, candidate.z + pz - sz},
            {candidate.x - px + sx, candidate.z - pz + sz},
            {candidate.x - px - sx, candidate.z - pz - sz},
        };
        for (const Vec2& p : corners) {
            if (!world.isPassable(p.x, p.y)) return false;
        }
        return true;
    };

    if (canMoveTo(next)) {
        pos_ = next;
        pos_.y = static_cast<float>(surfaceAt(next));
        crashFlash_ = std::max(0.0f, crashFlash_ - dt);
    } else {
        // Try sliding along one axis so glancing hits do not stop the car.
        Vec3 slideX{next.x, pos_.y, pos_.z};
        Vec3 slideZ{pos_.x, pos_.y, next.z};
        if (canMoveTo(slideX)) {
            pos_ = slideX;
            pos_.y = static_cast<float>(surfaceAt(slideX));
            speed_ *= 0.86f;
        } else if (canMoveTo(slideZ)) {
            pos_ = slideZ;
            pos_.y = static_cast<float>(surfaceAt(slideZ));
            speed_ *= 0.86f;
        } else {
            crashFlash_ = 0.25f;
            speed_ = -speed_ * 0.18f;
            if (std::abs(speed_) < 0.2f) speed_ = 0.0f;
        }
    }

    // Small cosmetic suspension bob on the current road surface.
    bob_ = std::sin(speed_ * 0.8f) * 0.01f * std::min(1.0f, std::abs(speed_) / 6.0f);
    const int ground =
        world.roadSurface(static_cast<int>(std::floor(pos_.x)),
                          static_cast<int>(std::floor(pos_.z)));
    pos_.y = static_cast<float>(ground > 0 ? ground : world.surfaceHeight(
                  static_cast<int>(std::floor(pos_.x)),
                  static_cast<int>(std::floor(pos_.z)))) + bob_;
}

} // namespace gta
