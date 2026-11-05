#include "graphics/shader.h"

#include <cstdio>
#include <vector>

namespace gta {

namespace shaders {

const char* kSpriteVertex = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform mat4 uMVP;
out vec2 vUV;
out vec4 vColor;
void main() {
    vUV = aUV;
    vColor = aColor;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";

const char* kSpriteFragment = R"GLSL(
#version 330 core
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uTexture;
uniform int uAlphaTest;
uniform int uMask;
uniform vec2 uMaskCenter;
uniform float uMaskRadius;
out vec4 FragColor;
void main() {
    if (uMask == 1 && distance(vUV, uMaskCenter) > uMaskRadius) discard;
    vec4 texel = texture(uTexture, vUV);
    // uAlphaTest: 0 = blend, 1 = alpha test, 2 = opaque geometry (ignore
    // the palette colour-key alpha used by map decals).
    if (uAlphaTest == 2) {
        texel.a = 1.0;
    } else if (uAlphaTest == 1 && texel.a < 0.5) {
        discard;
    }
    FragColor = texel * vColor;
}
)GLSL";

const char* kFullscreenVertex = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
out vec2 vUV;
void main() {
    vUV = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

// Compact FXAA implementation (luma based edge blend).
const char* kFxaaFragment = R"GLSL(
#version 330 core
in vec2 vUV;
uniform sampler2D uTexture;
uniform vec2 uTexelSize;
out vec4 FragColor;

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
    vec3 rgbNW = texture(uTexture, vUV + vec2(-1.0, -1.0) * uTexelSize).rgb;
    vec3 rgbNE = texture(uTexture, vUV + vec2( 1.0, -1.0) * uTexelSize).rgb;
    vec3 rgbSW = texture(uTexture, vUV + vec2(-1.0,  1.0) * uTexelSize).rgb;
    vec3 rgbSE = texture(uTexture, vUV + vec2( 1.0,  1.0) * uTexelSize).rgb;
    vec4 center = texture(uTexture, vUV);
    vec3 rgbM = center.rgb;

    float lNW = luma(rgbNW);
    float lNE = luma(rgbNE);
    float lSW = luma(rgbSW);
    float lSE = luma(rgbSE);
    float lM  = luma(rgbM);

    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    if (lMax - lMin < max(0.0312, lMax * 0.125)) {
        FragColor = center;
        return;
    }

    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)),
                     ((lNW + lSW) - (lNE + lSE)));
    float dirReduce = max((lNW + lNE + lSW + lSE) * 0.03125, 1.0 / 128.0);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-8.0), vec2(8.0)) * uTexelSize;

    vec3 rgbA = 0.5 * (texture(uTexture, vUV + dir * (1.0 / 3.0 - 0.5)).rgb +
                       texture(uTexture, vUV + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(uTexture, vUV + dir * -0.5).rgb +
                                     texture(uTexture, vUV + dir *  0.5).rgb);
    float lB = luma(rgbB);
    FragColor = vec4((lB < lMin || lB > lMax) ? rgbA : rgbB, center.a);
}
)GLSL";

} // namespace shaders

namespace {

#ifdef GTA_GLES
// The shader sources target desktop GLSL 330.  On Android we translate the
// version line and inject the precision qualifiers required by GLSL ES 3.00.
std::string toGlesSource(const char* source) {
    std::string text(source ? source : "");
    const std::string version = "#version 330 core";
    const std::string replacement =
        "#version 300 es\nprecision highp float;\nprecision highp int;\n";
    const size_t pos = text.find(version);
    if (pos != std::string::npos) {
        text.replace(pos, version.size(), replacement);
    }
    return text;
}
#endif

GLuint compileStage(GLenum type, const char* source, const std::string& name) {
#ifdef GTA_GLES
    std::string gles = toGlesSource(source);
    const char* stageSource = gles.c_str();
#else
    const char* stageSource = source;
#endif
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &stageSource, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(length > 1 ? length : 1);
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        std::fprintf(stderr, "[shader] %s (%s) failed:\n%s\n", name.c_str(),
                     type == GL_VERTEX_SHADER ? "vertex" : "fragment", log.data());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

Shader::~Shader() { destroy(); }

void Shader::destroy() {
    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }
}

bool Shader::compile(const char* vertexSource, const char* fragmentSource,
                     const std::string& debugName) {
    destroy();

    GLuint vs = compileStage(GL_VERTEX_SHADER, vertexSource, debugName);
    if (!vs) return false;
    GLuint fs = compileStage(GL_FRAGMENT_SHADER, fragmentSource, debugName);
    if (!fs) {
        glDeleteShader(vs);
        return false;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(length > 1 ? length : 1);
        glGetProgramInfoLog(program, length, nullptr, log.data());
        std::fprintf(stderr, "[shader] %s link failed:\n%s\n", debugName.c_str(), log.data());
        glDeleteProgram(program);
        return false;
    }

    program_ = program;
    return true;
}

void Shader::setInt(const char* name, int value) const {
    glUniform1i(glGetUniformLocation(program_, name), value);
}

void Shader::setFloat(const char* name, float value) const {
    glUniform1f(glGetUniformLocation(program_, name), value);
}

void Shader::setVec4(const char* name, float x, float y, float z, float w) const {
    glUniform4f(glGetUniformLocation(program_, name), x, y, z, w);
}

void Shader::setVec2(const char* name, float x, float y) const {
    glUniform2f(glGetUniformLocation(program_, name), x, y);
}

void Shader::setMat4(const char* name, const Mat4& matrix) const {
    glUniformMatrix4fv(glGetUniformLocation(program_, name), 1, GL_FALSE, matrix.m);
}

} // namespace gta
