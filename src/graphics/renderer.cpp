#include "graphics/renderer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace gta {

namespace {

bool checkFramebuffer(const char* what) {
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "[renderer] %s framebuffer incomplete (0x%x)\n", what,
                     status);
        return false;
    }
    return true;
}

void writeU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
}

void writeU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

} // namespace

Renderer::~Renderer() { destroy(); }

bool Renderer::create(const GraphicsConfig& config, const std::string& title,
                      std::string& error) {
    destroy();

    samples_ = std::clamp(config.msaa, 0, 16);
    renderScale_ = std::clamp(config.renderScale, 0.25f, 2.0f);
    fxaa_ = config.fxaa;
    fullscreen_ = config.fullscreen;

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        error = std::string("SDL video initialisation failed: ") + SDL_GetError();
        return false;
    }

    if (!createWindow(config, title, error)) return false;

    context_ = SDL_GL_CreateContext(window_);
    if (!context_) {
        error = std::string("failed to create OpenGL 3.3 context: ") + SDL_GetError();
        destroy();
        return false;
    }
    SDL_GL_MakeCurrent(window_, context_);
    SDL_GL_SetSwapInterval(config.vsync ? 1 : 0);

    if (!spriteShader_.compile(shaders::kSpriteVertex, shaders::kSpriteFragment,
                               "sprite")) {
        error = "failed to compile the sprite shader";
        destroy();
        return false;
    }
    if (!fxaaShader_.compile(shaders::kFullscreenVertex, shaders::kFxaaFragment,
                             "fxaa")) {
        error = "failed to compile the FXAA shader";
        destroy();
        return false;
    }

    const uint8_t white[4] = {255, 255, 255, 255};
    if (!whiteTexture_.create(1, 1, white, true)) {
        error = "failed to create the helper texture";
        destroy();
        return false;
    }

    initFullscreenGeometry();
    if (!createSceneTargets(error)) {
        destroy();
        return false;
    }

    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return true;
}

bool Renderer::createWindow(const GraphicsConfig& config,
                            const std::string& title, std::string& error) {
#ifdef GTA_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
                        SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    const int requestedSamples = std::clamp(config.msaa, 0, 16);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, requestedSamples > 0 ? 1 : 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, requestedSamples);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                   SDL_WINDOW_ALLOW_HIGHDPI;
    if (config.fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    window_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, config.width, config.height,
                               flags);
    if (!window_) {
        error = std::string("failed to create window: ") + SDL_GetError();
        return false;
    }

    SDL_GL_GetDrawableSize(window_, &windowW_, &windowH_);
    if (windowW_ <= 0 || windowH_ <= 0) {
        SDL_GetWindowSize(window_, &windowW_, &windowH_);
    }
    renderW_ = std::max(320, static_cast<int>(windowW_ * renderScale_));
    renderH_ = std::max(200, static_cast<int>(windowH_ * renderScale_));
    return true;
}

void Renderer::destroy() {
    if (context_) {
        destroySceneTargets();
        if (fullscreenVbo_) {
            glDeleteBuffers(1, &fullscreenVbo_);
            fullscreenVbo_ = 0;
        }
        if (fullscreenVao_) {
            glDeleteVertexArrays(1, &fullscreenVao_);
            fullscreenVao_ = 0;
        }
        whiteTexture_.destroy();
        spriteShader_.destroy();
        fxaaShader_.destroy();
    }

    if (context_) {
        SDL_GL_DeleteContext(context_);
        context_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
}

void Renderer::initFullscreenGeometry() {
    if (fullscreenVao_) return;
    const float verts[] = {
        -1.0f, -1.0f,
         3.0f, -1.0f,
        -1.0f,  3.0f,
    };
    glGenVertexArrays(1, &fullscreenVao_);
    glGenBuffers(1, &fullscreenVbo_);
    glBindVertexArray(fullscreenVao_);
    glBindBuffer(GL_ARRAY_BUFFER, fullscreenVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
}

void Renderer::destroySceneTargets() {
    if (resolveDepth_) {
        glDeleteRenderbuffers(1, &resolveDepth_);
        resolveDepth_ = 0;
    }
    if (msaaDepth_) {
        glDeleteRenderbuffers(1, &msaaDepth_);
        msaaDepth_ = 0;
    }
    if (msaaColor_) {
        glDeleteRenderbuffers(1, &msaaColor_);
        msaaColor_ = 0;
    }
    if (resolveTexture_) {
        glDeleteTextures(1, &resolveTexture_);
        resolveTexture_ = 0;
    }
    if (resolveFbo_) {
        glDeleteFramebuffers(1, &resolveFbo_);
        resolveFbo_ = 0;
    }
    if (msaaFbo_) {
        glDeleteFramebuffers(1, &msaaFbo_);
        msaaFbo_ = 0;
    }
}

bool Renderer::createSceneTargets(std::string& error) {
    destroySceneTargets();

    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    samples_ = std::min(samples_, static_cast<int>(maxSamples));

    renderW_ = std::max(320, static_cast<int>(windowW_ * renderScale_));
    renderH_ = std::max(200, static_cast<int>(windowH_ * renderScale_));

    // Resolve target (also the direct render target when MSAA is disabled).
    glGenFramebuffers(1, &resolveFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo_);

    glGenTextures(1, &resolveTexture_);
    glBindTexture(GL_TEXTURE_2D, resolveTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, renderW_, renderH_, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           resolveTexture_, 0);

    if (samples_ <= 0) {
        glGenRenderbuffers(1, &resolveDepth_);
        glBindRenderbuffer(GL_RENDERBUFFER, resolveDepth_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, renderW_,
                              renderH_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, resolveDepth_);
    }
    if (!checkFramebuffer("resolve")) {
        error = "unable to create the scene framebuffer";
        return false;
    }

    if (samples_ > 0) {
        glGenFramebuffers(1, &msaaFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);

        glGenRenderbuffers(1, &msaaColor_);
        glBindRenderbuffer(GL_RENDERBUFFER, msaaColor_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_RGBA8,
                                         renderW_, renderH_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_RENDERBUFFER, msaaColor_);

        glGenRenderbuffers(1, &msaaDepth_);
        glBindRenderbuffer(GL_RENDERBUFFER, msaaDepth_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_,
                                         GL_DEPTH24_STENCIL8, renderW_, renderH_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, msaaDepth_);

        if (!checkFramebuffer("multisample")) {
            error = "unable to create the multisample framebuffer";
            return false;
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void Renderer::beginFrame() {
    glBindFramebuffer(GL_FRAMEBUFFER, samples_ > 0 ? msaaFbo_ : resolveFbo_);
    glViewport(0, 0, renderW_, renderH_);
    glClearColor(0.055f, 0.075f, 0.105f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer::endFrame() {
    if (samples_ > 0) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFbo_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo_);
        glBlitFramebuffer(0, 0, renderW_, renderH_, 0, 0, renderW_, renderH_,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, windowW_, windowH_);

    if (fxaa_ && resolveTexture_) {
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        fxaaShader_.use();
        fxaaShader_.setInt("uTexture", 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, resolveTexture_);
        glUniform2f(glGetUniformLocation(fxaaShader_.id(), "uTexelSize"),
                    1.0f / static_cast<float>(renderW_),
                    1.0f / static_cast<float>(renderH_));
        glBindVertexArray(fullscreenVao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
    } else if (samples_ > 0) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, resolveFbo_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, renderW_, renderH_, 0, 0, windowW_, windowH_,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
    } else {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, resolveFbo_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, renderW_, renderH_, 0, 0, windowW_, windowH_,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }

    SDL_GL_SwapWindow(window_);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
}

void Renderer::updateViewport() {
    SDL_GL_GetDrawableSize(window_, &windowW_, &windowH_);
    if (windowW_ <= 0 || windowH_ <= 0) {
        SDL_GetWindowSize(window_, &windowW_, &windowH_);
    }
    const int newW = std::max(320, static_cast<int>(windowW_ * renderScale_));
    const int newH = std::max(200, static_cast<int>(windowH_ * renderScale_));
    if (newW == renderW_ && newH == renderH_ && resolveFbo_) return;
    renderW_ = newW;
    renderH_ = newH;
    std::string error;
    if (!createSceneTargets(error)) {
        std::fprintf(stderr, "[renderer] %s\n", error.c_str());
    }
}

void Renderer::resize(int windowWidth, int windowHeight) {
    if (!window_) return;
    if (windowWidth > 0 && windowHeight > 0) {
        SDL_SetWindowSize(window_, windowWidth, windowHeight);
    }
    updateViewport();
}

void Renderer::setWindowSize(int width, int height) {
    if (!window_) return;
    SDL_SetWindowSize(window_, std::max(320, width), std::max(200, height));
    updateViewport();
}

void Renderer::setFullscreen(bool fullscreen) {
    if (!window_ || fullscreen_ == fullscreen) return;
    if (SDL_SetWindowFullscreen(window_, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
        std::fprintf(stderr, "[renderer] fullscreen change failed: %s\n",
                     SDL_GetError());
        return;
    }
    fullscreen_ = fullscreen;
    updateViewport();
}

void Renderer::toggleFullscreen() { setFullscreen(!fullscreen_); }

void Renderer::setVSync(bool enabled) {
    SDL_GL_SetSwapInterval(enabled ? 1 : 0);
}

void Renderer::setFxaa(bool enabled) { fxaa_ = enabled; }

void Renderer::setMSAA(int samples) {
    samples_ = std::clamp(samples, 0, 16);
    std::string error;
    if (!createSceneTargets(error)) {
        std::fprintf(stderr, "[renderer] %s\n", error.c_str());
    }
}

void Renderer::setRenderScale(float scale) {
    renderScale_ = std::clamp(scale, 0.25f, 2.0f);
    updateViewport();
}

bool Renderer::saveScreenshot(const std::string& path, std::string& error) {
    if (!window_) {
        error = "no window to capture";
        return false;
    }
    const int w = windowW_;
    const int h = windowH_;
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
#ifndef GTA_GLES
    glReadBuffer(GL_FRONT);
#endif
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
#ifndef GTA_GLES
    glReadBuffer(GL_BACK);
#endif

    const int rowSize = w * 3;
    const int padding = (4 - (rowSize % 4)) % 4;
    const int dataSize = (rowSize + padding) * h;
    const uint32_t fileSize = 54 + dataSize;

    std::vector<uint8_t> bmp;
    bmp.reserve(static_cast<size_t>(fileSize));
    bmp.push_back('B');
    bmp.push_back('M');
    writeU32(bmp, fileSize);
    writeU16(bmp, 0);
    writeU16(bmp, 0);
    writeU32(bmp, 54);
    writeU32(bmp, 40);
    writeU32(bmp, static_cast<uint32_t>(w));
    writeU32(bmp, static_cast<uint32_t>(h));
    writeU16(bmp, 1);
    writeU16(bmp, 24);
    writeU32(bmp, 0);
    writeU32(bmp, static_cast<uint32_t>(dataSize));
    writeU32(bmp, 2835);
    writeU32(bmp, 2835);
    writeU32(bmp, 0);
    writeU32(bmp, 0);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t o = (static_cast<size_t>(y) * w + x) * 4;
            bmp.push_back(rgba[o + 2]);
            bmp.push_back(rgba[o + 1]);
            bmp.push_back(rgba[o + 0]);
        }
        for (int p = 0; p < padding; ++p) bmp.push_back(0);
    }

    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
        error = "cannot open screenshot file: " + path;
        return false;
    }
    const size_t written = std::fwrite(bmp.data(), 1, bmp.size(), file);
    std::fclose(file);
    if (written != bmp.size()) {
        error = "short write while saving screenshot: " + path;
        return false;
    }
    return true;
}

} // namespace gta
