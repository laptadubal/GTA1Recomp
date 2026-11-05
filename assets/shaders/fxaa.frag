#version 330 core
// Compact luma-based FXAA post-processing pass.

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
