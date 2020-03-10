#ifndef ANIMATION_H
#define ANIMATION_H

#include "backend/Backend.h"
#include <net/Fetch.h>
#include <image/Decoder.h>
#include <dawn/webgpu_cpp.h>
#include <dawn_native/DawnNative.h>
#include <memory>

typedef struct GLFWwindow GLFWwindow;

class Animation
{
public:
    void create(GLFWwindow* window, int width, int height);
    void frame();

    void init();

private:
    std::unique_ptr<dawn_native::Instance> instance;
    wgpu::Device device;
    wgpu::Queue queue;
    wgpu::SwapChain swapchain;
    wgpu::Buffer indexBuffer;
    wgpu::Buffer vertexBuffer;
    wgpu::Texture texture;
    wgpu::Sampler sampler;
    wgpu::TextureView depthStencilView;
    wgpu::RenderPipeline pipeline;
    wgpu::BindGroup bindGroup;
    GLFWwindow* mWindow { nullptr };

    int width { 0 }, height { 0 };
    std::shared_ptr<BackendBinding> binding;
    std::shared_ptr<reckoning::net::Fetch> fetch;
    std::shared_ptr<reckoning::image::Decoder> decoder;

    std::vector<wgpu::RenderBundle> bundles;
};

#endif // ANIMATION_H
