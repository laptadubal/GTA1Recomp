#include "graphics/camera.h"

#include <algorithm>
#include <cmath>

namespace gta {

namespace {
constexpr float kNear = 0.15f;
constexpr float kFar = 900.0f;
} // namespace

void Camera::setViewport(int width, int height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
}

void Camera::setZoom(float zoom) {
    zoom_ = std::clamp(zoom, 0.35f, 3.0f);
}

void Camera::setTilt(float degrees) {
    tilt_ = std::clamp(degrees, 20.0f, 89.0f);
}

void Camera::snapTo(const Vec3& target) {
    target_ = target;
}

void Camera::follow(const Vec3& target, float dt, bool immediate) {
    if (immediate) {
        target_ = target;
        return;
    }
    const float factor = 1.0f - std::exp(-std::clamp(dt, 0.0f, 0.1f) * 7.5f);
    target_.x += (target.x - target_.x) * factor;
    target_.y += (target.y - target_.y) * factor;
    target_.z += (target.z - target_.z) * factor;
}

Vec3 Camera::eye() const {
    // Pull the eye back far enough that the requested ground height fits the
    // vertical field of view.  Zooming changes the distance, keeping the
    // perspective (and therefore the GTA1 look) intact.
    const float fovRadians = radians(fov_);
    const float distance =
        viewHeight_ / (2.0f * std::tan(fovRadians * 0.5f)) / zoom_;
    const float angle = radians(tilt_);
    return Vec3{target_.x, target_.y + std::sin(angle) * distance,
                target_.z + std::cos(angle) * distance};
}

Mat4 Camera::view() const {
    return Mat4::lookAt(eye(), target_, Vec3{0.0f, 1.0f, 0.0f});
}

Mat4 Camera::projection() const {
    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    return Mat4::perspective(radians(fov_), aspect, kNear, kFar);
}

Mat4 Camera::viewProjection() const { return projection() * view(); }

} // namespace gta
