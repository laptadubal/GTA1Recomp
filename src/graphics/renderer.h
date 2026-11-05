#pragma once

#include <string>

#include <SDL.h>

#include "core/config.h"
#include "graphics/gl.h"
#include "graphics/shader.h"
#include "graphics/texture.h"

namespace gta {

// Owns the SDL window, the OpenGL context and the off-screen scene target.
// The game renders into a multisampled FBO; endFrame() resolves it and either
// blits it to the window or runs the FXAA post-process pass.
class Renderer {
public:
    Renderer() = default;
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool create(const GraphicsConfig& config, const std::string& title,
                std::string& error);
    void destroy();

    void beginFrame();
    void endFrame();

    void resize(int windowWidth, int windowHeight);
    void setFullscreen(bool fullscreen);
    void toggleFullscreen();
    void setVSync(bool enabled);
    void setFxaa(bool enabled);
    void setMSAA(int samples);
    void setRenderScale(float scale);
    void setWindowSize(int width, int height);

    bool saveScreenshot(const std::string& path, std::string& error);

    SDL_Window* window() const { return window_; }
    int windowWidth() const { return windowW_; }
    int windowHeight() const { return windowH_; }
    int width() const { return renderW_; }
    int height() const { return renderH_; }
    float aspect() const {
        return renderH_ > 0 ? static_cast<float>(renderW_) / renderH_ : 1.0f;
    }

    bool fullscreen() const { return fullscreen_; }
    bool fxaaEnabled() const { return fxaa_; }
    int msaa() const { return samples_; }
    float renderScale() const { return renderScale_; }

    Shader& spriteShader() { return spriteShader_; }
    Shader& fxaaShader() { return fxaaShader_; }
    Texture& whiteTexture() { return whiteTexture_; }

private:
    bool createWindow(const GraphicsConfig& config, const std::string& title,
                      std::string& error);
    bool createSceneTargets(std::string& error);
    void destroySceneTargets();
    void updateViewport();
    void initFullscreenGeometry();

    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;

    int windowW_ = 1280;
    int windowH_ = 720;
    int renderW_ = 1280;
    int renderH_ = 720;
    int samples_ = 0;
    float renderScale_ = 1.0f;
    bool fullscreen_ = false;
    bool fxaa_ = true;

    GLuint msaaFbo_ = 0;
    GLuint msaaColor_ = 0;
    GLuint msaaDepth_ = 0;
    GLuint resolveFbo_ = 0;
    GLuint resolveTexture_ = 0;
    GLuint resolveDepth_ = 0;

    GLuint fullscreenVao_ = 0;
    GLuint fullscreenVbo_ = 0;

    Shader spriteShader_;
    Shader fxaaShader_;
    Texture whiteTexture_;
};

} // namespace gta
