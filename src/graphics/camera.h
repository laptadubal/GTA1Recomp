#pragma once

#include "core/math3d.h"

namespace gta {

// Fixed-angle follow camera used by the top-down GTA view.
class Camera {
public:
    void setViewport(int width, int height);
    void setZoom(float zoom);
    float zoom() const { return zoom_; }

    void setTilt(float degrees);
    float tilt() const { return tilt_; }

    void snapTo(const Vec3& target);
    void follow(const Vec3& target, float dt, bool immediate = false);

    const Vec3& target() const { return target_; }
    Vec3 eye() const;
    Mat4 viewProjection() const;
    Mat4 view() const;
    Mat4 projection() const;

    // Approximate ground-plane height visible at the target at zoom 1.
    float viewHeight() const { return viewHeight_; }

private:
    int width_ = 1280;
    int height_ = 720;
    float zoom_ = 1.0f;
    // GTA1 uses a fairly strong three-quarter view: the walls of buildings
    // are clearly visible while the street layout stays readable.
    float tilt_ = 66.0f;
    float fov_ = 34.0f;
    // World units visible vertically at the target at zoom 1.
    float viewHeight_ = 11.0f;
    Vec3 target_{128.0f, 0.0f, 128.0f};
};

} // namespace gta
