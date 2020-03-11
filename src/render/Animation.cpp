#include "Animation.h"
#include "Constants.h"
#include "Utils.h"
#include <log/Log.h>
#include <dawn/dawn_proc.h>
#include <shaderc/shaderc.hpp>
#include <memory>
#include <cassert>
#include <glm/vec4.hpp>
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

#ifdef __APPLE__
static constexpr wgpu::BackendType backendType = wgpu::BackendType::Metal;
#else
static constexpr wgpu::BackendType backendType = wgpu::BackendType::Vulkan;
#endif

struct UniformGeometry
{
    glm::vec4 geometry;
};

void Animation::create(GLFWwindow* window, int w, int h)
{
    Log(Log::Info) << "go me";

    width = w;
    height = h;

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

    binding = makeBackendBinding(window, backendDevice);

    dawnProcSetProcs(&backendProcs);
    backendProcs.deviceSetUncapturedErrorCallback(backendDevice, PrintDeviceError, nullptr);
    device = wgpu::Device::Acquire(backendDevice);

    auto GetSwapChain = [this](const wgpu::Device& device) {
        wgpu::SwapChainDescriptor swapChainDesc;
        swapChainDesc.implementation = binding->GetSwapChainImplementation();
        return device.CreateSwapChain(nullptr, &swapChainDesc);
    };

    auto GetPreferredSwapChainTextureFormat = [this]() {
        return static_cast<wgpu::TextureFormat>(binding->GetPreferredSwapChainTextureFormat());
    };

    queue = device.CreateQueue();
    swapchain = GetSwapChain(device);
    swapchain.Configure(GetPreferredSwapChainTextureFormat(), wgpu::TextureUsage::OutputAttachment, width, height);

    wgpu::FenceDescriptor descriptor;
    descriptor.initialValue = fenceValue;
    fence = queue.CreateFence(&descriptor);
}

void Animation::init()
{
    fetch = net::Fetch::create();
    decoder = image::Decoder::create();

    fetch->fetch("https://www.google.com/images/branding/googlelogo/2x/googlelogo_color_272x92dp.png").then([this](std::shared_ptr<buffer::Buffer>&& buffer) -> auto& {
        if (!buffer) {
            return reckoning::then::rejected<image::Decoder::Image>("no buffer from fetch");
        }
        return decoder->decode(std::move(buffer), kTextureRowPitchAlignment);
    }).then([this](image::Decoder::Image&& image) -> void {
        if (!image.data) {
            return;
        }

        auto GetPreferredSwapChainTextureFormat = [this]() {
            return static_cast<wgpu::TextureFormat>(binding->GetPreferredSwapChainTextureFormat());
        };

        // auto initBuffers = [this]() {
            // static const uint32_t indexData[3] = {
            //     0, 1, 2,
            // };
            // indexBuffer = CreateBufferFromData(device, indexData, sizeof(indexData),
            //                                    wgpu::BufferUsage::Index);

            // static const float vertexData[12] = {
            //     0.0f, 0.5f, 0.0f, 1.0f,
            //     -0.5f, -0.5f, 0.0f, 1.0f,
            //     0.5f, -0.5f, 0.0f, 1.0f,
            // };
            // vertexBuffer = CreateBufferFromData(device, vertexData, sizeof(vertexData),
            //                                     wgpu::BufferUsage::Vertex);
        // };

        auto initTextures = [this, &image]() {
            wgpu::TextureDescriptor descriptor;
            descriptor.dimension = wgpu::TextureDimension::e2D;
            descriptor.size.width = image.width;
            descriptor.size.height = image.height;
            descriptor.size.depth = 1;
            descriptor.arrayLayerCount = 1;
            descriptor.sampleCount = 1;
            descriptor.format = wgpu::TextureFormat::RGBA8Unorm;
            descriptor.mipLevelCount = 1;
            descriptor.usage = wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::Sampled;
            texture = device.CreateTexture(&descriptor);

            wgpu::SamplerDescriptor samplerDesc = GetDefaultSamplerDescriptor();
            sampler = device.CreateSampler(&samplerDesc);

            wgpu::Buffer stagingBuffer = CreateBufferFromData(
                device, image.data->data(), static_cast<uint32_t>(image.data->size()), wgpu::BufferUsage::CopySrc);
            wgpu::BufferCopyView bufferCopyView = CreateBufferCopyView(stagingBuffer, 0, image.bpl, 0);
            wgpu::TextureCopyView textureCopyView = CreateTextureCopyView(texture, 0, 0, {0, 0, 0});
            wgpu::Extent3D copySize = {image.width, image.height, 1};

            wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
            encoder.CopyBufferToTexture(&bufferCopyView, &textureCopyView, &copySize);

            wgpu::CommandBuffer copy = encoder.Finish();
            queue.Submit(1, &copy);
        };

        // initBuffers();
        initTextures();

        // wgpu::ShaderModule vsModule =
        // CreateShaderModule(device, SingleShaderStage::Vertex, R"(
        // #version 450
        // layout(location = 0) in vec4 pos;
        // void main() {
        //     gl_Position = pos;
        // })");

        wgpu::ShaderModule vsModule =
        CreateShaderModule(device, SingleShaderStage::Vertex, R"(
        #version 450

        layout(set = 0, binding = 2) uniform UniformBufferObject {
            vec4 geometry;
        } ubo;

        vec2 positions[4] = vec2[](
            vec2(-1.0, +1.0),
            vec2(+1.0, +1.0),
            vec2(-1.0, -1.0),
            vec2(+1.0, -1.0)
        );

        void main() {
            vec2 position = positions[gl_VertexIndex];
            int x = position.x == -1.0 ? 0 : 2;
            int y = position.y == +1.0 ? 1 : 3;
            gl_Position = vec4(ubo.geometry[x], ubo.geometry[y], 0.0, 1.0);
        })");

        wgpu::ShaderModule fsModule =
        CreateShaderModule(device, SingleShaderStage::Fragment, R"(
        #version 450
        layout(set = 0, binding = 0) uniform sampler mySampler;
        layout(set = 0, binding = 1) uniform texture2D myTexture;

        layout(location = 0) out vec4 fragColor;
        void main() {
            fragColor = texture(sampler2D(myTexture, mySampler), gl_FragCoord.xy / vec2(544.0, 184.0));
        })");

        auto bgl = MakeBindGroupLayout(
            device, {
                {0, wgpu::ShaderStage::Fragment, wgpu::BindingType::Sampler},
                {1, wgpu::ShaderStage::Fragment, wgpu::BindingType::SampledTexture},
                {2, wgpu::ShaderStage::Vertex, wgpu::BindingType::UniformBuffer}
            });

        wgpu::PipelineLayout pl = MakeBasicPipelineLayout(device, &bgl);

        depthStencilView = CreateDefaultDepthStencilView(device, width, height);

        ComboRenderPipelineDescriptor descriptor(device);
        descriptor.layout = MakeBasicPipelineLayout(device, &bgl);
        descriptor.vertexStage.module = vsModule;
        descriptor.cFragmentStage.module = fsModule;
        descriptor.primitiveTopology = wgpu::PrimitiveTopology::TriangleStrip;
        // descriptor.cVertexState.vertexBufferCount = 1;
        // descriptor.cVertexState.cVertexBuffers[0].arrayStride = 4 * sizeof(float);
        // descriptor.cVertexState.cVertexBuffers[0].attributeCount = 1;
        // descriptor.cVertexState.cAttributes[0].format = wgpu::VertexFormat::Float4;
        descriptor.depthStencilState = &descriptor.cDepthStencilState;
        descriptor.cDepthStencilState.format = wgpu::TextureFormat::Depth24PlusStencil8;
        descriptor.cColorStates[0].format = GetPreferredSwapChainTextureFormat();
        descriptor.cColorStates[0].colorBlend.srcFactor = wgpu::BlendFactor::SrcAlpha;
        descriptor.cColorStates[0].colorBlend.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;

        pipeline = device.CreateRenderPipeline(&descriptor);

        wgpu::TextureView view = texture.CreateView();

        UniformGeometry geom = { { -1.0, 1.0, 1.0, -1.0 } };

        wgpu::Buffer ubo = CreateBufferFromData(
            device, &geom, sizeof(geom), wgpu::BufferUsage::Uniform);

        bindGroup = MakeBindGroup(device, bgl, {
                {0, sampler},
                {1, view},
                {2, ubo}
            });

        ComboRenderBundleEncoderDescriptor bundleDescriptor;
        bundleDescriptor.colorFormatsCount = 1;
        bundleDescriptor.cColorFormats[0] = GetPreferredSwapChainTextureFormat();
        bundleDescriptor.depthStencilFormat = wgpu::TextureFormat::Depth24PlusStencil8;

        wgpu::RenderBundleEncoder renderBundleEncoder = device.CreateRenderBundleEncoder(&bundleDescriptor);
        renderBundleEncoder.SetPipeline(pipeline);
        renderBundleEncoder.SetBindGroup(0, bindGroup);
        // renderBundleEncoder.SetVertexBuffer(0, vertexBuffer);
        // renderBundleEncoder.SetIndexBuffer(indexBuffer);
        // renderBundleEncoder.DrawIndexed(3, 1, 0, 0, 0);
        renderBundleEncoder.Draw(4, 1, 0, 0);
        wgpu::RenderBundle bundle = renderBundleEncoder.Finish();

        bundles.push_back(bundle);
    });
}

void Animation::frame()
{
    wgpu::TextureView backbufferView = swapchain.GetCurrentTextureView();
    ComboRenderPassDescriptor renderPass({backbufferView}, depthStencilView);

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    {
        wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&renderPass);
        if (!bundles.empty()) {
            pass.ExecuteBundles(bundles.size(), &bundles[0]);
        }
        pass.EndPass();
    }

    wgpu::CommandBuffer commands = encoder.Finish();
    queue.Submit(1, &commands);
    swapchain.Present();
}
