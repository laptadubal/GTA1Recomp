#include "graphics/bitmap_font.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <vector>

#include "core/math3d.h"

namespace gta {

namespace {

struct RawGlyph {
    int width = 0;
    bool ink = false;
    std::vector<uint8_t> pixels;
};

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size <= 0) return false;
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(out.data()), size);
    return static_cast<bool>(file);
}

struct FontVertex {
    float x, y, u, v, r, g, b, a;
};

} // namespace

BitmapFont::~BitmapFont() { destroy(); }

void BitmapFont::destroy() {
    if (vbo_) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    texture_.destroy();
    glyphs_.clear();
    height_ = 0;
    capacity_ = 0;
}

bool BitmapFont::load(const std::string& path, std::string& error) {
    destroy();
    std::vector<uint8_t> data;
    if (!readFile(path, data)) {
        error = "cannot read font: " + path;
        return false;
    }
    if (data.size() < 3) {
        error = "font file too small: " + path;
        return false;
    }

    size_t pos = 0;
    const int count = data[pos++];
    height_ = data[pos++];
    if (height_ <= 0 || count <= 0) {
        error = "invalid font header";
        return false;
    }

    std::vector<RawGlyph> raw(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        if (pos >= data.size()) {
            error = "truncated font glyph table";
            return false;
        }
        RawGlyph& glyph = raw[static_cast<size_t>(i)];
        glyph.width = data[pos++];
        const size_t bytes =
            static_cast<size_t>(glyph.width) * static_cast<size_t>(height_);
        if (pos + bytes > data.size()) {
            error = "truncated font glyph data";
            return false;
        }
        glyph.pixels.assign(data.begin() + static_cast<std::ptrdiff_t>(pos),
                            data.begin() + static_cast<std::ptrdiff_t>(pos + bytes));
        glyph.ink = std::any_of(glyph.pixels.begin(), glyph.pixels.end(),
                                [](uint8_t v) { return v != 0; });
        pos += bytes;
    }
    if (pos + 768 > data.size()) {
        error = "font palette is missing";
        return false;
    }
    const uint8_t* palette = data.data() + pos;

    int totalWidth = 0;
    for (const RawGlyph& glyph : raw) {
        totalWidth += glyph.width + 1;
    }
    int atlasWidth = 1;
    while (atlasWidth < totalWidth + 4) atlasWidth <<= 1;
    const int atlasHeight = height_;

    std::vector<uint8_t> pixels(
        static_cast<size_t>(atlasWidth) * atlasHeight * 4, 0);
    glyphs_.resize(static_cast<size_t>(count));
    int x = 0;
    for (int i = 0; i < count; ++i) {
        const RawGlyph& glyph = raw[static_cast<size_t>(i)];
        Glyph& out = glyphs_[static_cast<size_t>(i)];
        out.width = glyph.width;
        out.ink = glyph.ink;
        out.u0 = static_cast<float>(x) / atlasWidth;
        out.u1 = static_cast<float>(x + glyph.width) / atlasWidth;
        for (int y = 0; y < height_; ++y) {
            for (int gx = 0; gx < glyph.width; ++gx) {
                const uint8_t index =
                    glyph.pixels[static_cast<size_t>(y) * glyph.width + gx];
                if (index == 0) continue;
                const size_t o =
                    (static_cast<size_t>(y) * atlasWidth + x + gx) * 4;
                pixels[o + 0] = palette[static_cast<size_t>(index) * 3 + 0];
                pixels[o + 1] = palette[static_cast<size_t>(index) * 3 + 1];
                pixels[o + 2] = palette[static_cast<size_t>(index) * 3 + 2];
                pixels[o + 3] = 255;
            }
        }
        x += glyph.width + 1;
    }

    if (!texture_.create(atlasWidth, atlasHeight, pixels.data(), true)) {
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

int BitmapFont::glyphIndex(char ch) const {
    // GTA .FON glyph 0 is '!' (ASCII 33), so the table is simply ASCII - 33.
    // Fonts with 140 glyphs (CUTTEXT, F_MTEXT, ...) include real lowercase
    // letters; older 118-glyph fonts leave those slots blank, in which case
    // resolveGlyph() falls back to the uppercase form.
    const unsigned char c = static_cast<unsigned char>(ch);
    if (c == ' ') return -1;
    if (c < 33 || c > 126) return -2;
    return static_cast<int>(c) - 33;
}

int BitmapFont::resolveGlyph(char ch) const {
    const int index = glyphIndex(ch);
    if (index < 0) return index;
    const int count = static_cast<int>(glyphs_.size());
    if (index < count && glyphs_[static_cast<size_t>(index)].width > 0 &&
        glyphs_[static_cast<size_t>(index)].ink) {
        return index;
    }
    // A blank lowercase slot: fall back to the uppercase glyph.
    const unsigned char c = static_cast<unsigned char>(ch);
    if (c >= 'a' && c <= 'z') {
        const int upper = static_cast<int>(c) - 'a' + 32;
        if (upper < count && glyphs_[static_cast<size_t>(upper)].width > 0) {
            return upper;
        }
    }
    return index < count ? index : -2;
}

float BitmapFont::measure(const std::string& text, float scale) const {
    float width = 0.0f;
    for (char c : text) {
        const int index = resolveGlyph(c);
        if (index < 0) {
            if (c == ' ') width += 10.0f * scale;
            continue;
        }
        if (index >= static_cast<int>(glyphs_.size())) continue;
        width += static_cast<float>(glyphs_[static_cast<size_t>(index)].width + 2) *
                 scale;
    }
    return width > 0.0f ? width - 2.0f * scale : 0.0f;
}

void BitmapFont::draw(Shader& shader, const std::string& text, float x, float y,
                      float scale, float r, float g, float b, float a,
                      int screenWidth, int screenHeight) const {
    if (!texture_.valid() || glyphs_.empty()) return;
    std::vector<FontVertex> vertices;
    vertices.reserve(text.size() * 6);

    const float glyphHeight = static_cast<float>(height_) * scale;
    float penX = x;
    float penY = y;
    for (char c : text) {
        if (c == '\n') {
            penX = x;
            penY += (static_cast<float>(height_) + 4.0f) * scale;
            continue;
        }
        const int index = resolveGlyph(c);
        if (index < 0) {
            if (c == ' ') penX += 10.0f * scale;
            continue;
        }
        if (index >= static_cast<int>(glyphs_.size())) continue;
        const Glyph& glyph = glyphs_[static_cast<size_t>(index)];
        const float w = static_cast<float>(glyph.width) * scale;
        const float x0 = penX;
        const float x1 = penX + w;
        vertices.push_back({x0, penY, glyph.u0, 0.0f, r, g, b, a});
        vertices.push_back({x1, penY, glyph.u1, 0.0f, r, g, b, a});
        vertices.push_back({x1, penY + glyphHeight, glyph.u1, 1.0f, r, g, b, a});
        vertices.push_back({x0, penY, glyph.u0, 0.0f, r, g, b, a});
        vertices.push_back({x1, penY + glyphHeight, glyph.u1, 1.0f, r, g, b, a});
        vertices.push_back({x0, penY + glyphHeight, glyph.u0, 1.0f, r, g, b, a});
        penX += w + 2.0f * scale;
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


void BitmapFont::drawGlyph(Shader& shader, int index, float x, float y,
                           float scale, float r, float g, float b, float a,
                           int screenWidth, int screenHeight) const {
    if (!texture_.valid() || index < 0 ||
        index >= static_cast<int>(glyphs_.size())) {
        return;
    }
    const Glyph& glyph = glyphs_[static_cast<size_t>(index)];
    const float w = static_cast<float>(glyph.width) * scale;
    const float h = static_cast<float>(height_) * scale;
    FontVertex vertices[6] = {
        {x, y, glyph.u0, 0.0f, r, g, b, a},
        {x + w, y, glyph.u1, 0.0f, r, g, b, a},
        {x + w, y + h, glyph.u1, 1.0f, r, g, b, a},
        {x, y, glyph.u0, 0.0f, r, g, b, a},
        {x + w, y + h, glyph.u1, 1.0f, r, g, b, a},
        {x, y + h, glyph.u0, 1.0f, r, g, b, a},
    };
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
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace gta

