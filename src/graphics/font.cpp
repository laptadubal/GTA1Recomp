#include "graphics/font.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/math3d.h"

namespace gta {

namespace {

constexpr int kGlyphW = 5;
constexpr int kGlyphH = 7;
constexpr int kAdvance = 6;
constexpr int kCell = 8;
constexpr int kCols = 16;
constexpr int kRows = 8;
constexpr int kTexW = kCols * kCell;
constexpr int kTexH = kRows * kCell;

// ASCII 32..95, seven rows of five bits each (bit 4 = leftmost pixel).
const uint8_t kGlyphs[64][kGlyphH] = {
    {0, 0, 0, 0, 0, 0, 0},                            // space
    {0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0, 0b00100}, // !
    {0b01010, 0b01010, 0, 0, 0, 0, 0},                // "
    {0b01010, 0b11111, 0b01010, 0b01010, 0b11111, 0b01010, 0}, // #
    {0b00100, 0b01111, 0b10100, 0b01110, 0b00101, 0b11110, 0b00100}, // $
    {0b11001, 0b11010, 0b00100, 0b01000, 0b10110, 0b00110, 0}, // %
    {0b01100, 0b10010, 0b10100, 0b01000, 0b10101, 0b10010, 0b01101}, // &
    {0b00100, 0b00100, 0, 0, 0, 0, 0},                // '
    {0b00010, 0b00100, 0b01000, 0b01000, 0b01000, 0b00100, 0b00010}, // (
    {0b01000, 0b00100, 0b00010, 0b00010, 0b00010, 0b00100, 0b01000}, // )
    {0, 0b00100, 0b10101, 0b01110, 0b10101, 0b00100, 0}, // *
    {0, 0b00100, 0b00100, 0b11111, 0b00100, 0b00100, 0}, // +
    {0, 0, 0, 0, 0, 0b00100, 0b01000},                // ,
    {0, 0, 0, 0b11111, 0, 0, 0},                      // -
    {0, 0, 0, 0, 0, 0, 0b00100},                      // .
    {0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0, 0}, // /
    {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110}, // 0
    {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}, // 1
    {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111}, // 2
    {0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110}, // 3
    {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010}, // 4
    {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110}, // 5
    {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110}, // 6
    {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000}, // 7
    {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110}, // 8
    {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100}, // 9
    {0, 0b00100, 0b00100, 0, 0b00100, 0b00100, 0},    // :
    {0, 0b00100, 0b00100, 0, 0b00100, 0b00100, 0b01000}, // ;
    {0b00010, 0b00100, 0b01000, 0b10000, 0b01000, 0b00100, 0b00010}, // <
    {0, 0, 0b11111, 0, 0b11111, 0, 0},                // =
    {0b01000, 0b00100, 0b00010, 0b00001, 0b00010, 0b00100, 0b01000}, // >
    {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0, 0b00100}, // ?
    {0b01110, 0b10001, 0b10111, 0b10101, 0b10111, 0b10000, 0b01110}, // @
    {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}, // A
    {0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110}, // B
    {0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110}, // C
    {0b11100, 0b10010, 0b10001, 0b10001, 0b10001, 0b10010, 0b11100}, // D
    {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111}, // E
    {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000}, // F
    {0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01111}, // G
    {0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}, // H
    {0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}, // I
    {0b00111, 0b00010, 0b00010, 0b00010, 0b00010, 0b10010, 0b01100}, // J
    {0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001}, // K
    {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111}, // L
    {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001}, // M
    {0b10001, 0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001}, // N
    {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}, // O
    {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000}, // P
    {0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101}, // Q
    {0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001}, // R
    {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110}, // S
    {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100}, // T
    {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}, // U
    {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100}, // V
    {0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b11011, 0b10001}, // W
    {0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001}, // X
    {0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100}, // Y
    {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111}, // Z
    {0b01110, 0b01000, 0b01000, 0b01000, 0b01000, 0b01000, 0b01110}, // [
    {0b10000, 0b01000, 0b00100, 0b00010, 0b00001, 0, 0}, // backslash
    {0b01110, 0b00010, 0b00010, 0b00010, 0b00010, 0b00010, 0b01110}, // ]
    {0b00100, 0b01010, 0b10001, 0, 0, 0, 0},          // ^
    {0, 0, 0, 0, 0, 0, 0b11111},                      // _
};

struct FontVertex {
    float x, y, u, v, r, g, b, a;
};

int glyphFor(char ch) {
    unsigned char c = static_cast<unsigned char>(ch);
    if (c >= 'a' && c <= 'z') c = static_cast<unsigned char>(c - 'a' + 'A');
    if (c < 32 || c > 95) return '?' - 32;
    return static_cast<int>(c) - 32;
}

} // namespace

Font::~Font() { destroy(); }

bool Font::create(std::string& error) {
    destroy();

    std::vector<uint8_t> pixels(static_cast<size_t>(kTexW) * kTexH * 4, 0);
    for (int glyph = 0; glyph < 64; ++glyph) {
        const int cellX = (glyph % kCols) * kCell;
        const int cellY = (glyph / kCols) * kCell;
        for (int row = 0; row < kGlyphH; ++row) {
            const uint8_t bits = kGlyphs[glyph][row];
            for (int col = 0; col < kGlyphW; ++col) {
                if ((bits & (1u << (kGlyphW - 1 - col))) == 0) continue;
                const int px = cellX + 1 + col;
                const int py = cellY + 1 + row;
                const size_t o = (static_cast<size_t>(py) * kTexW + px) * 4;
                pixels[o + 0] = 255;
                pixels[o + 1] = 255;
                pixels[o + 2] = 255;
                pixels[o + 3] = 255;
            }
        }
    }

    if (!texture_.create(kTexW, kTexH, pixels.data(), true)) {
        error = "failed to create the font texture";
        return false;
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(FontVertex),
                          reinterpret_cast<void*>(offsetof(FontVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(FontVertex),
                          reinterpret_cast<void*>(offsetof(FontVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(FontVertex),
                          reinterpret_cast<void*>(offsetof(FontVertex, r)));
    glBindVertexArray(0);
    return true;
}

void Font::destroy() {
    if (vbo_) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    texture_.destroy();
    capacity_ = 0;
}

float Font::measure(const std::string& text, float scale) const {
    if (text.empty()) return 0.0f;
    return (static_cast<float>(text.size()) * kAdvance - 1.0f) * scale;
}

void Font::draw(Shader& shader, const std::string& text, float x, float y,
                float scale, float r, float g, float b, float a, int screenWidth,
                int screenHeight) {
    if (text.empty() || !vao_ || !texture_.valid()) return;

    std::vector<FontVertex> vertices;
    vertices.reserve(text.size() * 6);

    float penX = x;
    for (char ch : text) {
        if (ch == '\n') {
            penX = x;
            y += (kGlyphH + 3) * scale;
            continue;
        }
        if (ch == ' ') {
            penX += kAdvance * scale;
            continue;
        }
        const int glyph = glyphFor(ch);
        const int cellX = (glyph % kCols) * kCell;
        const int cellY = (glyph / kCols) * kCell;
        const float u0 = static_cast<float>(cellX + 1) / kTexW;
        const float v0 = static_cast<float>(cellY + 1) / kTexH;
        const float u1 = static_cast<float>(cellX + 1 + kGlyphW) / kTexW;
        const float v1 = static_cast<float>(cellY + 1 + kGlyphH) / kTexH;

        const float x0 = penX;
        const float y0 = y;
        const float x1 = penX + kGlyphW * scale;
        const float y1 = y + kGlyphH * scale;

        vertices.push_back({x0, y0, u0, v0, r, g, b, a});
        vertices.push_back({x1, y0, u1, v0, r, g, b, a});
        vertices.push_back({x1, y1, u1, v1, r, g, b, a});
        vertices.push_back({x0, y0, u0, v0, r, g, b, a});
        vertices.push_back({x1, y1, u1, v1, r, g, b, a});
        vertices.push_back({x0, y1, u0, v1, r, g, b, a});

        penX += kAdvance * scale;
    }

    if (vertices.empty()) return;

    const Mat4 mvp = Mat4::ortho(0.0f, static_cast<float>(screenWidth),
                                 static_cast<float>(screenHeight), 0.0f, -1.0f,
                                 1.0f);
    shader.use();
    shader.setMat4("uMVP", mvp);
    shader.setInt("uTexture", 0);
    shader.setInt("uAlphaTest", 1);
    texture_.bind(0);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const GLsizeiptr bytes =
        static_cast<GLsizeiptr>(vertices.size() * sizeof(FontVertex));
    if (static_cast<GLsizei>(vertices.size()) > capacity_) {
        capacity_ = static_cast<GLsizei>(vertices.size());
        glBufferData(GL_ARRAY_BUFFER, bytes, vertices.data(), GL_STREAM_DRAW);
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, vertices.data());
    }
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace gta
