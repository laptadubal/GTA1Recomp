#version 330 core
// Reference copy of the built-in sprite/city shader.

in vec2 vUV;
in vec4 vColor;

uniform sampler2D uTexture;
uniform int uAlphaTest;

out vec4 FragColor;

void main() {
    vec4 texel = texture(uTexture, vUV);
    if (uAlphaTest == 1 && texel.a < 0.5) discard;
    FragColor = texel * vColor;
}
