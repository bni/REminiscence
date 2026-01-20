#include "shader.h"
#include "util.h"
#include <SDL3/SDL.h>
#include <fstream>
#include <sstream>

// Flashback native resolution
static const int kGameWidth = 256;
static const int kGameHeight = 224;

static std::string loadShaderFile() {
    std::string path = SDL_GetBasePath();
    path += "shader.metal";
    std::ifstream file(path);
    if (!file.is_open()) {
        debug(DBG_INFO, "Shader: Failed to open shader file: %s", path.c_str());
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

Shader::Shader()
    : m_renderer(nullptr),
      m_device(nullptr),
      m_fragmentShader(nullptr),
      m_renderState(nullptr),
      m_params{},
      m_available(false) {
    m_params.res_x = static_cast<float>(kGameWidth);
    m_params.res_y = static_cast<float>(kGameHeight);
}

Shader::~Shader() {
    shutdown();
}

bool Shader::init(SDL_Renderer *renderer) {
    m_renderer = renderer;

    // Get the GPU device from the renderer
    m_device = SDL_GetGPURendererDevice(renderer);
    if (!m_device) {
        warning("Shader: Not a GPU renderer, falling back to simple render");
        return false;
    }

    // Check what shader formats are supported
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(m_device);
    if (!(formats & SDL_GPU_SHADERFORMAT_MSL)) {
        warning("Shader: MSL shader format not supported");
        return false;
    }

    if (!loadShader()) {
        return false;
    }

    m_available = true;
    debug(DBG_INFO, "Shader initialized successfully");
    return true;
}

bool Shader::loadShader() {
    // Load shader source from file
    const std::string shaderSource = loadShaderFile();
    if (shaderSource.empty()) {
        debug(DBG_INFO, "Shader: Failed to load shader file");
        return false;
    }

    // Create the fragment shader
    SDL_GPUShaderCreateInfo shaderInfo = {};
    shaderInfo.code = reinterpret_cast<const Uint8 *>(shaderSource.c_str());
    shaderInfo.code_size = shaderSource.size();
    shaderInfo.entrypoint = "Fragment";
    shaderInfo.format = SDL_GPU_SHADERFORMAT_MSL;
    shaderInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    shaderInfo.num_samplers = 1;
    shaderInfo.num_storage_textures = 0;
    shaderInfo.num_storage_buffers = 0;
    shaderInfo.num_uniform_buffers = 1;

    m_fragmentShader = SDL_CreateGPUShader(m_device, &shaderInfo);
    if (!m_fragmentShader) {
        warning("Shader: Failed to create fragment shader: %s", SDL_GetError());
        return false;
    }

    // Create the render state
    SDL_GPURenderStateCreateInfo stateInfo = {};
    stateInfo.fragment_shader = m_fragmentShader;
    stateInfo.num_sampler_bindings = 0;
    stateInfo.sampler_bindings = nullptr;
    stateInfo.num_storage_textures = 0;
    stateInfo.storage_textures = nullptr;
    stateInfo.num_storage_buffers = 0;
    stateInfo.storage_buffers = nullptr;
    stateInfo.props = 0;

    m_renderState = SDL_CreateGPURenderState(m_renderer, &stateInfo);
    if (!m_renderState) {
        warning("Shader: Failed to create render state: %s", SDL_GetError());
        SDL_ReleaseGPUShader(m_device, m_fragmentShader);
        m_fragmentShader = nullptr;
        return false;
    }

    return true;
}

void Shader::shutdown() {
    if (m_renderState) {
        SDL_DestroyGPURenderState(m_renderState);
        m_renderState = nullptr;
    }

    if (m_fragmentShader && m_device) {
        SDL_ReleaseGPUShader(m_device, m_fragmentShader);
        m_fragmentShader = nullptr;
    }

    m_available = false;
    m_device = nullptr;
    m_renderer = nullptr;
}

void Shader::setUniforms(const SDL_FRect &outputRect, const int screenWidth, const int screenHeight) {
    if (!m_available || !m_renderState) {
        return;
    }

    m_params.trg_x = outputRect.x;
    m_params.trg_y = outputRect.y;
    m_params.trg_w = outputRect.w;
    m_params.trg_h = outputRect.h;
    m_params.scr_w = static_cast<float>(screenWidth);
    m_params.scr_h = static_cast<float>(screenHeight);

    SDL_SetGPURenderStateFragmentUniforms(m_renderState, 0, &m_params, sizeof(m_params));
}

void Shader::enable() const {
    if (!m_available || !m_renderState || !m_renderer) {
        return;
    }

    SDL_SetGPURenderState(m_renderer, m_renderState);
}

void Shader::disable() const {
    if (!m_renderer) {
        return;
    }

    SDL_SetGPURenderState(m_renderer, nullptr);
}
