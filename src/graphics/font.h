#pragma once

#include <string>

#include "graphics/gl.h"
#include "graphics/shader.h"
#include "graphics/texture.h"

namespace gta {

// Small built-in bitmap font (5x7) so the HUD does not depend on system fonts.
class Font {
public:
    Font() = default;
    ~Font();
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    bool create(std::string& error);
    void destroy();

    void draw(Shader& shader, const std::string& text, float x, float y,
              float scale, float r, float g, float b, float a, int screenWidth,
              int screenHeight);

    float measure(const std::string& text, float scale) const;

private:
    Texture texture_;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLsizei capacity_ = 0;
};

} // namespace gta
