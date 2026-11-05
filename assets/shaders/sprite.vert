#version 330 core
// Reference copy of the built-in sprite/city shader.  The runtime uses the
// identical source embedded in src/graphics/shader.cpp so a data directory is
// not required for the game to start.

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
