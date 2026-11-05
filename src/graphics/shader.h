#pragma once

#include <string>

#include "core/math3d.h"
#include "graphics/gl.h"

namespace gta {

class Shader {
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    bool compile(const char* vertexSource, const char* fragmentSource,
                 const std::string& debugName = "shader");
    void destroy();

    void use() const { glUseProgram(program_); }
    GLuint id() const { return program_; }
    bool valid() const { return program_ != 0; }

    void setInt(const char* name, int value) const;
    void setFloat(const char* name, float value) const;
    void setVec4(const char* name, float x, float y, float z, float w) const;
    void setVec2(const char* name, float x, float y) const;
    void setMat4(const char* name, const Mat4& matrix) const;

private:
    GLuint program_ = 0;
};

namespace shaders {

// Textured, vertex-coloured quads used for city blocks, sprites and HUD.
extern const char* kSpriteVertex;
extern const char* kSpriteFragment;

// Fullscreen FXAA pass.
extern const char* kFullscreenVertex;
extern const char* kFxaaFragment;

} // namespace shaders
} // namespace gta
