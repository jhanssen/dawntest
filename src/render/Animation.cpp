#include "Animation.h"
#include "Utils.h"
#include <log/Log.h>
#include <event/Loop.h>
#include <dawn/dawn_proc.h>
#include <shaderc/shaderc.hpp>
#include <memory>
#include <cassert>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

using namespace reckoning;
using namespace reckoning::log;
using namespace std::chrono_literals;

static void PrintDeviceError(WGPUErrorType errorType, const char* message, void*) {
    const char* errorTypeName = "";
    switch (errorType) {
    case WGPUErrorType_Validation:
        errorTypeName = "Validation";
        break;
    case WGPUErrorType_OutOfMemory:
        errorTypeName = "Out of memory";
        break;
    case WGPUErrorType_Unknown:
        errorTypeName = "Unknown";
        break;
    case WGPUErrorType_DeviceLost:
        errorTypeName = "Device lost";
        break;
    default:
        return;
    }
    Log(Log::Error) << errorTypeName << "error:" << message;
}

class AbstractBackendBinding {
public:
    virtual ~AbstractBackendBinding() = default;

    virtual uint64_t GetSwapChainImplementation() = 0;
    virtual WGPUTextureFormat GetPreferredSwapChainTextureFormat() = 0;

protected:
    AbstractBackendBinding(GLFWwindow* window, WGPUDevice device);

    GLFWwindow* mWindow = nullptr;
    WGPUDevice mDevice = nullptr;
};

AbstractBackendBinding::AbstractBackendBinding(GLFWwindow* window, WGPUDevice device)
    : mWindow(window), mDevice(device) {
}

#ifdef __APPLE__
static constexpr wgpu::BackendType backendType = wgpu::BackendType::Metal;
#else
#include <dawn_native/VulkanBackend.h>

static constexpr wgpu::BackendType backendType = wgpu::BackendType::Vulkan;

class BackendBinding : public AbstractBackendBinding {
public:
    BackendBinding(GLFWwindow* window, WGPUDevice device) : AbstractBackendBinding(window, device) {
    }

    uint64_t GetSwapChainImplementation() override {
        if (mSwapchainImpl.userData == nullptr) {
            VkSurfaceKHR surface = VK_NULL_HANDLE;
            if (glfwCreateWindowSurface(dawn_native::vulkan::GetInstance(mDevice), mWindow,
                                        nullptr, &surface) != VK_SUCCESS) {
                assert(false);
            }

            mSwapchainImpl = dawn_native::vulkan::CreateNativeSwapChainImpl(mDevice, surface);
        }
        return reinterpret_cast<uint64_t>(&mSwapchainImpl);
    }
    WGPUTextureFormat GetPreferredSwapChainTextureFormat() override {
        assert(mSwapchainImpl.userData != nullptr);
        return dawn_native::vulkan::GetNativeSwapChainPreferredFormat(&mSwapchainImpl);
    }

private:
    DawnSwapChainImplementation mSwapchainImpl = {};
};
#endif

void Animation::init(GLFWwindow* window, int width, int height)
{
    Log(Log::Info) << "go me";

    mWindow = window;

    instance = std::make_unique<dawn_native::Instance>();
    instance->DiscoverDefaultAdapters();

    dawn_native::Adapter backendAdapter;
    {
        std::vector<dawn_native::Adapter> adapters = instance->GetAdapters();
        auto adapterIt = std::find_if(adapters.begin(), adapters.end(),
                                      [](const dawn_native::Adapter adapter) -> bool {
                                          wgpu::AdapterProperties properties;
                                          adapter.GetProperties(&properties);
                                          return properties.backendType == backendType;
                                      });
        assert(adapterIt != adapters.end());
        backendAdapter = *adapterIt;
    }

    WGPUDevice backendDevice = backendAdapter.CreateDevice();
    DawnProcTable backendProcs = dawn_native::GetProcs();

    std::shared_ptr<BackendBinding> binding = std::make_shared<BackendBinding>(window, backendDevice);

    dawnProcSetProcs(&backendProcs);
    backendProcs.deviceSetUncapturedErrorCallback(backendDevice, PrintDeviceError, nullptr);
    device = wgpu::Device::Acquire(backendDevice);

    auto GetSwapChain = [&binding](const wgpu::Device& device) {
        wgpu::SwapChainDescriptor swapChainDesc;
        swapChainDesc.implementation = binding->GetSwapChainImplementation();
        return device.CreateSwapChain(nullptr, &swapChainDesc);
    };

    auto GetPreferredSwapChainTextureFormat = [&binding]() {
        return static_cast<wgpu::TextureFormat>(binding->GetPreferredSwapChainTextureFormat());
    };

    queue = device.CreateQueue();
    swapchain = GetSwapChain(device);
    swapchain.Configure(GetPreferredSwapChainTextureFormat(), wgpu::TextureUsage::OutputAttachment, width, height);

    auto initBuffers = [this]() {
        static const uint32_t indexData[3] = {
            0, 1, 2,
        };
        indexBuffer = CreateBufferFromData(device, indexData, sizeof(indexData),
                                           wgpu::BufferUsage::Index);

        static const float vertexData[12] = {
            0.0f, 0.5f, 0.0f, 1.0f,
            -0.5f, -0.5f, 0.0f, 1.0f,
            0.5f, -0.5f, 0.0f, 1.0f,
        };
        vertexBuffer = CreateBufferFromData(device, vertexData, sizeof(vertexData),
                                            wgpu::BufferUsage::Vertex);
    };

    auto initTextures = [this]() {
        wgpu::TextureDescriptor descriptor;
        descriptor.dimension = wgpu::TextureDimension::e2D;
        descriptor.size.width = 1024;
        descriptor.size.height = 1024;
        descriptor.size.depth = 1;
        descriptor.arrayLayerCount = 1;
        descriptor.sampleCount = 1;
        descriptor.format = wgpu::TextureFormat::RGBA8Unorm;
        descriptor.mipLevelCount = 1;
        descriptor.usage = wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::Sampled;
        texture = device.CreateTexture(&descriptor);

        wgpu::SamplerDescriptor samplerDesc = GetDefaultSamplerDescriptor();
        sampler = device.CreateSampler(&samplerDesc);

        // Initialize the texture with arbitrary data until we can load images
        std::vector<uint8_t> data(4 * 1024 * 1024, 0);
        for (size_t i = 0; i < data.size(); ++i) {
            data[i] = static_cast<uint8_t>(i % 253);
        }

        wgpu::Buffer stagingBuffer = CreateBufferFromData(
            device, data.data(), static_cast<uint32_t>(data.size()), wgpu::BufferUsage::CopySrc);
        wgpu::BufferCopyView bufferCopyView = CreateBufferCopyView(stagingBuffer, 0, 0, 0);
        wgpu::TextureCopyView textureCopyView = CreateTextureCopyView(texture, 0, 0, {0, 0, 0});
        wgpu::Extent3D copySize = {1024, 1024, 1};

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        encoder.CopyBufferToTexture(&bufferCopyView, &textureCopyView, &copySize);

        wgpu::CommandBuffer copy = encoder.Finish();
        queue.Submit(1, &copy);
    };

    initBuffers();
    initTextures();

    wgpu::ShaderModule vsModule =
    CreateShaderModule(device, SingleShaderStage::Vertex, R"(
        #version 450
        layout(location = 0) in vec4 pos;
        void main() {
            gl_Position = pos;
        })");

    wgpu::ShaderModule fsModule =
    CreateShaderModule(device, SingleShaderStage::Fragment, R"(
        #version 450
        layout(set = 0, binding = 0) uniform sampler mySampler;
        layout(set = 0, binding = 1) uniform texture2D myTexture;

        layout(location = 0) out vec4 fragColor;
        void main() {
            fragColor = texture(sampler2D(myTexture, mySampler), gl_FragCoord.xy / vec2(640.0, 480.0));
        })");

    auto bgl = MakeBindGroupLayout(
        device, {
            {0, wgpu::ShaderStage::Fragment, wgpu::BindingType::Sampler},
            {1, wgpu::ShaderStage::Fragment, wgpu::BindingType::SampledTexture},
        });

    wgpu::PipelineLayout pl = MakeBasicPipelineLayout(device, &bgl);

    depthStencilView = CreateDefaultDepthStencilView(device, width, height);

    ComboRenderPipelineDescriptor descriptor(device);
    descriptor.layout = MakeBasicPipelineLayout(device, &bgl);
    descriptor.vertexStage.module = vsModule;
    descriptor.cFragmentStage.module = fsModule;
    descriptor.cVertexState.vertexBufferCount = 1;
    descriptor.cVertexState.cVertexBuffers[0].arrayStride = 4 * sizeof(float);
    descriptor.cVertexState.cVertexBuffers[0].attributeCount = 1;
    descriptor.cVertexState.cAttributes[0].format = wgpu::VertexFormat::Float4;
    descriptor.depthStencilState = &descriptor.cDepthStencilState;
    descriptor.cDepthStencilState.format = wgpu::TextureFormat::Depth24PlusStencil8;
    descriptor.cColorStates[0].format = GetPreferredSwapChainTextureFormat();

    pipeline = device.CreateRenderPipeline(&descriptor);

    wgpu::TextureView view = texture.CreateView();

    bindGroup = MakeBindGroup(device, bgl, {
            {0, sampler},
            {1, view}
        });

}

void Animation::run()
{
    std::shared_ptr<event::Loop> loop = event::Loop::create();

    for (;;) {
        frame();
        loop->execute(16ms);
        if (loop->stopped())
            break;
    }

    // not thread safe?
    if (mWindow) {
        glfwSetWindowShouldClose(mWindow, 1);
    }
}

struct {uint32_t a; float b;} s;
void Animation::frame()
{
    s.a = (s.a + 1) % 256;
    s.b += 0.02f;
    if (s.b >= 1.0f) {s.b = 0.0f;}

    wgpu::TextureView backbufferView = swapchain.GetCurrentTextureView();
    ComboRenderPassDescriptor renderPass({backbufferView}, depthStencilView);

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    {
        wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&renderPass);
        pass.SetPipeline(pipeline);
        pass.SetBindGroup(0, bindGroup);
        pass.SetVertexBuffer(0, vertexBuffer);
        pass.SetIndexBuffer(indexBuffer);
        pass.DrawIndexed(3, 1, 0, 0, 0);
        pass.EndPass();
    }

    wgpu::CommandBuffer commands = encoder.Finish();
    queue.Submit(1, &commands);
    swapchain.Present();
}
