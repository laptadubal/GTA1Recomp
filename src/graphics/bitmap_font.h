#pragma once

#include <string>
#include <vector>

#include "graphics/gl.h"
#include "graphics/shader.h"
#include "graphics/texture.h"

namespace gta {

// Loads an original GTA .FON bitmap font (BIG1.FON, BIG2.FON, ...).
// The format is: uint8 count, uint8 height, then per glyph uint8 width and
// width*height palette indices, followed by a 768-byte RGB palette.
class BitmapFont {
public:
    BitmapFont() = default;
    ~BitmapFont();
    BitmapFont(const BitmapFont&) = delete;
    BitmapFont& operator=(const BitmapFont&) = delete;

    bool load(const std::string& path, std::string& error);
    void destroy();

    bool valid() const { return texture_.valid(); }
    int height() const { return height_; }

    float measure(const std::string& text, float scale) const;
    void draw(Shader& shader, const std::string& text, float x, float y,
              float scale, float r, float g, float b, float a, int screenWidth,
              int screenHeight) const;

    // Draws one glyph (used for the animated key cursor in F_KEY.FON).
    void drawGlyph(Shader& shader, int index, float x, float y, float scale,
                   float r, float g, float b, float a, int screenWidth,
                   int screenHeight) const;

private:
    struct Glyph {
        int width = 0;
        bool ink = false;
        float u0 = 0.0f;
        float u1 = 0.0f;
    };

    int glyphIndex(char c) const;
    int resolveGlyph(char c) const;

    Texture texture_;
    std::vector<Glyph> glyphs_;
    int height_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    mutable GLsizei capacity_ = 0;
};

} // namespace gta
