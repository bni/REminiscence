#ifndef SHADER_H
#define SHADER_H

#include <SDL3/SDL.h>

struct Params {
    float trg_x; // Target rectangle X position
    float trg_y; // Target rectangle Y position
    float trg_w; // Target rectangle width
    float trg_h; // Target rectangle height
    float scr_w; // Screen width
    float scr_h; // Screen height
    float res_x; // Emulated resolution X (256)
    float res_y; // Emulated resolution Y (224)
};

class Shader {
public:
    Shader();

    ~Shader();

    bool init(SDL_Renderer *renderer);

    void shutdown();

    void setUniforms(const SDL_FRect &outputRect, int screenWidth, int screenHeight);

    void enable() const;

    void disable() const;

    bool isAvailable() const { return m_available; }

private:
    bool loadShader();

    SDL_Renderer *m_renderer;
    SDL_GPUDevice *m_device;
    SDL_GPUShader *m_fragmentShader;
    SDL_GPURenderState *m_renderState;
    Params m_params;
    bool m_available;
};

#endif
