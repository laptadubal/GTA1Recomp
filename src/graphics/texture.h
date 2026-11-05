#pragma once

#include <cstdint>

#include "graphics/gl.h"

namespace gta {

class Texture {
public:
    Texture() = default;
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    bool create(int width, int height, const uint8_t* rgba, bool nearest = true);
    bool createEmpty(int width, int height, bool nearest = true);
    void destroy();

    void bind(int unit = 0) const;
    GLuint id() const { return id_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool valid() const { return id_ != 0; }

private:
    GLuint id_ = 0;
    int width_ = 0;
    int height_ = 0;
};

} // namespace gta
