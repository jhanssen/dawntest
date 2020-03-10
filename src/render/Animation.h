#ifndef ANIMATION_H
#define ANIMATION_H

#include <dawn/webgpu_cpp.h>
#include <dawn_native/DawnNative.h>
#include <memory>

typedef struct GLFWwindow GLFWwindow;

class Animation
{
public:
    void init(GLFWwindow* window, int width, int height);
    void run();

private:
    void frame();

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
};

#endif // ANIMATION_H
