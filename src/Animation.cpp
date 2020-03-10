#include "Animation.h"
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

static constexpr float kLodMin = 0.0;
static constexpr float kLodMax = 1000.0;
static constexpr uint32_t kMaxVertexBuffers = 16u;
static constexpr uint32_t kMaxVertexAttributes = 16u;
static constexpr uint32_t kMaxColorAttachments = 4u;

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

wgpu::Buffer CreateBufferFromData(const wgpu::Device& device,
                                  const void* data,
                                  uint64_t size,
                                  wgpu::BufferUsage usage) {
    wgpu::BufferDescriptor descriptor;
    descriptor.size = size;
    descriptor.usage = usage | wgpu::BufferUsage::CopyDst;

    wgpu::Buffer buffer = device.CreateBuffer(&descriptor);
    buffer.SetSubData(0, size, data);
    return buffer;
}

template <typename T>
wgpu::Buffer CreateBufferFromData(const wgpu::Device& device,
                                  wgpu::BufferUsage usage,
                                  std::initializer_list<T> data) {
    return CreateBufferFromData(device, data.begin(), uint32_t(sizeof(T) * data.size()), usage);
}

wgpu::SamplerDescriptor GetDefaultSamplerDescriptor() {
    wgpu::SamplerDescriptor desc;

    desc.minFilter = wgpu::FilterMode::Linear;
    desc.magFilter = wgpu::FilterMode::Linear;
    desc.mipmapFilter = wgpu::FilterMode::Linear;
    desc.addressModeU = wgpu::AddressMode::Repeat;
    desc.addressModeV = wgpu::AddressMode::Repeat;
    desc.addressModeW = wgpu::AddressMode::Repeat;
    desc.lodMinClamp = kLodMin;
    desc.lodMaxClamp = kLodMax;
    desc.compare = wgpu::CompareFunction::Never;

    return desc;
}

wgpu::BufferCopyView CreateBufferCopyView(wgpu::Buffer buffer,
                                          uint64_t offset,
                                          uint32_t rowPitch,
                                          uint32_t imageHeight) {
    wgpu::BufferCopyView bufferCopyView;
    bufferCopyView.buffer = buffer;
    bufferCopyView.offset = offset;
    bufferCopyView.rowPitch = rowPitch;
    bufferCopyView.imageHeight = imageHeight;

    return bufferCopyView;
}

wgpu::TextureCopyView CreateTextureCopyView(wgpu::Texture texture,
                                            uint32_t mipLevel,
                                            uint32_t arrayLayer,
                                            wgpu::Origin3D origin) {
    wgpu::TextureCopyView textureCopyView;
    textureCopyView.texture = texture;
    textureCopyView.mipLevel = mipLevel;
    textureCopyView.arrayLayer = arrayLayer;
    textureCopyView.origin = origin;

    return textureCopyView;
}

enum class SingleShaderStage { Vertex, Fragment, Compute };

shaderc_shader_kind ShadercShaderKind(SingleShaderStage stage) {
    switch (stage) {
    case SingleShaderStage::Vertex:
        return shaderc_glsl_vertex_shader;
    case SingleShaderStage::Fragment:
        return shaderc_glsl_fragment_shader;
    case SingleShaderStage::Compute:
        return shaderc_glsl_compute_shader;
    default:
        abort();
    }
}

wgpu::ShaderModule CreateShaderModuleFromResult(
    const wgpu::Device& device,
    const shaderc::SpvCompilationResult& result) {
    // result.cend and result.cbegin return pointers to uint32_t.
    const uint32_t* resultBegin = result.cbegin();
    const uint32_t* resultEnd = result.cend();
    // So this size is in units of sizeof(uint32_t).
    ptrdiff_t resultSize = resultEnd - resultBegin;
    // SetSource takes data as uint32_t*.

    wgpu::ShaderModuleDescriptor descriptor;
    descriptor.codeSize = static_cast<uint32_t>(resultSize);
    descriptor.code = result.cbegin();
    return device.CreateShaderModule(&descriptor);
}

wgpu::ShaderModule CreateShaderModule(const wgpu::Device& device,
                                      SingleShaderStage stage,
                                      const std::string& source) {
    shaderc_shader_kind kind = ShadercShaderKind(stage);

    shaderc::Compiler compiler;
    auto result = compiler.CompileGlslToSpv(source.c_str(), source.size(), kind, "myshader?");
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        Log(Log::Error) << result.GetErrorMessage();
        return {};
    }

    return CreateShaderModuleFromResult(device, result);
}

wgpu::BindGroupLayout MakeBindGroupLayout(
    const wgpu::Device& device,
    std::initializer_list<wgpu::BindGroupLayoutBinding> bindingsInitializer) {
    constexpr wgpu::ShaderStage kNoStages{};

    std::vector<wgpu::BindGroupLayoutBinding> bindings;
    for (const wgpu::BindGroupLayoutBinding& binding : bindingsInitializer) {
        if (binding.visibility != kNoStages) {
            bindings.push_back(binding);
        }
    }

    wgpu::BindGroupLayoutDescriptor descriptor;
    descriptor.bindingCount = static_cast<uint32_t>(bindings.size());
    descriptor.bindings = bindings.data();
    return device.CreateBindGroupLayout(&descriptor);
}

wgpu::TextureView CreateDefaultDepthStencilView(const wgpu::Device& device, uint32_t width, uint32_t height) {
    wgpu::TextureDescriptor descriptor;
    descriptor.dimension = wgpu::TextureDimension::e2D;
    descriptor.size.width = width;
    descriptor.size.height = height;
    descriptor.size.depth = 1;
    descriptor.arrayLayerCount = 1;
    descriptor.sampleCount = 1;
    descriptor.format = wgpu::TextureFormat::Depth24PlusStencil8;
    descriptor.mipLevelCount = 1;
    descriptor.usage = wgpu::TextureUsage::OutputAttachment;
    auto depthStencilTexture = device.CreateTexture(&descriptor);
    return depthStencilTexture.CreateView();
}

wgpu::PipelineLayout MakeBasicPipelineLayout(const wgpu::Device& device,
                                             const wgpu::BindGroupLayout* bindGroupLayout) {
    wgpu::PipelineLayoutDescriptor descriptor;
    if (bindGroupLayout != nullptr) {
        descriptor.bindGroupLayoutCount = 1;
        descriptor.bindGroupLayouts = bindGroupLayout;
    } else {
        descriptor.bindGroupLayoutCount = 0;
        descriptor.bindGroupLayouts = nullptr;
    }
    return device.CreatePipelineLayout(&descriptor);
}

struct BindingInitializationHelper {
    BindingInitializationHelper(uint32_t binding, const wgpu::Sampler& sampler);
    BindingInitializationHelper(uint32_t binding, const wgpu::TextureView& textureView);
    BindingInitializationHelper(uint32_t binding,
                                const wgpu::Buffer& buffer,
                                uint64_t offset = 0,
                                uint64_t size = wgpu::kWholeSize);

    wgpu::BindGroupBinding GetAsBinding() const;

    uint32_t binding;
    wgpu::Sampler sampler;
    wgpu::TextureView textureView;
    wgpu::Buffer buffer;
    uint64_t offset = 0;
    uint64_t size = 0;
};

BindingInitializationHelper::BindingInitializationHelper(uint32_t binding,
                                                         const wgpu::Sampler& sampler)
    : binding(binding), sampler(sampler) {
}

BindingInitializationHelper::BindingInitializationHelper(uint32_t binding,
                                                         const wgpu::TextureView& textureView)
    : binding(binding), textureView(textureView) {
}

BindingInitializationHelper::BindingInitializationHelper(uint32_t binding,
                                                         const wgpu::Buffer& buffer,
                                                         uint64_t offset,
                                                         uint64_t size)
    : binding(binding), buffer(buffer), offset(offset), size(size) {
}

wgpu::BindGroupBinding BindingInitializationHelper::GetAsBinding() const {
    wgpu::BindGroupBinding result;

    result.binding = binding;
    result.sampler = sampler;
    result.textureView = textureView;
    result.buffer = buffer;
    result.offset = offset;
    result.size = size;

    return result;
}

wgpu::BindGroup MakeBindGroup(
    const wgpu::Device& device,
    const wgpu::BindGroupLayout& layout,
    std::initializer_list<BindingInitializationHelper> bindingsInitializer) {
    std::vector<wgpu::BindGroupBinding> bindings;
    for (const BindingInitializationHelper& helper : bindingsInitializer) {
        bindings.push_back(helper.GetAsBinding());
    }

    wgpu::BindGroupDescriptor descriptor;
    descriptor.layout = layout;
    descriptor.bindingCount = bindings.size();
    descriptor.bindings = bindings.data();

    return device.CreateBindGroup(&descriptor);
}

class ComboVertexStateDescriptor : public wgpu::VertexStateDescriptor {
public:
    ComboVertexStateDescriptor();

    std::array<wgpu::VertexBufferLayoutDescriptor, kMaxVertexBuffers> cVertexBuffers;
    std::array<wgpu::VertexAttributeDescriptor, kMaxVertexAttributes> cAttributes;
};

class ComboRenderPipelineDescriptor : public wgpu::RenderPipelineDescriptor {
public:
    ComboRenderPipelineDescriptor(const wgpu::Device& device);

    ComboRenderPipelineDescriptor(const ComboRenderPipelineDescriptor&) = delete;
    ComboRenderPipelineDescriptor& operator=(const ComboRenderPipelineDescriptor&) = delete;
    ComboRenderPipelineDescriptor(ComboRenderPipelineDescriptor&&) = delete;
    ComboRenderPipelineDescriptor& operator=(ComboRenderPipelineDescriptor&&) = delete;

    wgpu::ProgrammableStageDescriptor cFragmentStage;

    ComboVertexStateDescriptor cVertexState;
    wgpu::RasterizationStateDescriptor cRasterizationState;
    std::array<wgpu::ColorStateDescriptor, kMaxColorAttachments> cColorStates;
    wgpu::DepthStencilStateDescriptor cDepthStencilState;
};

ComboVertexStateDescriptor::ComboVertexStateDescriptor() {
    wgpu::VertexStateDescriptor* descriptor = this;

    descriptor->indexFormat = wgpu::IndexFormat::Uint32;
    descriptor->vertexBufferCount = 0;

    // Fill the default values for vertexBuffers and vertexAttributes in buffers.
    wgpu::VertexAttributeDescriptor vertexAttribute;
    vertexAttribute.shaderLocation = 0;
    vertexAttribute.offset = 0;
    vertexAttribute.format = wgpu::VertexFormat::Float;
    for (uint32_t i = 0; i < kMaxVertexAttributes; ++i) {
        cAttributes[i] = vertexAttribute;
    }
    for (uint32_t i = 0; i < kMaxVertexBuffers; ++i) {
        cVertexBuffers[i].arrayStride = 0;
        cVertexBuffers[i].stepMode = wgpu::InputStepMode::Vertex;
        cVertexBuffers[i].attributeCount = 0;
        cVertexBuffers[i].attributes = nullptr;
    }
    // cVertexBuffers[i].attributes points to somewhere in cAttributes.
    // cVertexBuffers[0].attributes points to &cAttributes[0] by default. Assuming
    // cVertexBuffers[0] has two attributes, then cVertexBuffers[1].attributes should point to
    // &cAttributes[2]. Likewise, if cVertexBuffers[1] has 3 attributes, then
    // cVertexBuffers[2].attributes should point to &cAttributes[5].
    cVertexBuffers[0].attributes = &cAttributes[0];
    descriptor->vertexBuffers = &cVertexBuffers[0];
}

ComboRenderPipelineDescriptor::ComboRenderPipelineDescriptor(const wgpu::Device& device) {
    wgpu::RenderPipelineDescriptor* descriptor = this;

    descriptor->primitiveTopology = wgpu::PrimitiveTopology::TriangleList;
    descriptor->sampleCount = 1;

    // Set defaults for the vertex stage descriptor.
    { vertexStage.entryPoint = "main"; }

    // Set defaults for the fragment stage desriptor.
    {
        descriptor->fragmentStage = &cFragmentStage;
        cFragmentStage.entryPoint = "main";
    }

    // Set defaults for the input state descriptors.
    descriptor->vertexState = &cVertexState;

    // Set defaults for the rasterization state descriptor.
    {
        cRasterizationState.frontFace = wgpu::FrontFace::CCW;
        cRasterizationState.cullMode = wgpu::CullMode::None;

        cRasterizationState.depthBias = 0;
        cRasterizationState.depthBiasSlopeScale = 0.0;
        cRasterizationState.depthBiasClamp = 0.0;
        descriptor->rasterizationState = &cRasterizationState;
    }

    // Set defaults for the color state descriptors.
    {
        descriptor->colorStateCount = 1;
        descriptor->colorStates = cColorStates.data();

        wgpu::BlendDescriptor blend;
        blend.operation = wgpu::BlendOperation::Add;
        blend.srcFactor = wgpu::BlendFactor::One;
        blend.dstFactor = wgpu::BlendFactor::Zero;
        wgpu::ColorStateDescriptor colorStateDescriptor;
        colorStateDescriptor.format = wgpu::TextureFormat::RGBA8Unorm;
        colorStateDescriptor.alphaBlend = blend;
        colorStateDescriptor.colorBlend = blend;
        colorStateDescriptor.writeMask = wgpu::ColorWriteMask::All;
        for (uint32_t i = 0; i < kMaxColorAttachments; ++i) {
            cColorStates[i] = colorStateDescriptor;
        }
    }

    // Set defaults for the depth stencil state descriptors.
    {
        wgpu::StencilStateFaceDescriptor stencilFace;
        stencilFace.compare = wgpu::CompareFunction::Always;
        stencilFace.failOp = wgpu::StencilOperation::Keep;
        stencilFace.depthFailOp = wgpu::StencilOperation::Keep;
        stencilFace.passOp = wgpu::StencilOperation::Keep;

        cDepthStencilState.format = wgpu::TextureFormat::Depth24PlusStencil8;
        cDepthStencilState.depthWriteEnabled = false;
        cDepthStencilState.depthCompare = wgpu::CompareFunction::Always;
        cDepthStencilState.stencilBack = stencilFace;
        cDepthStencilState.stencilFront = stencilFace;
        cDepthStencilState.stencilReadMask = 0xff;
        cDepthStencilState.stencilWriteMask = 0xff;
        descriptor->depthStencilState = nullptr;
    }
}

struct ComboRenderPassDescriptor : public wgpu::RenderPassDescriptor {
public:
    ComboRenderPassDescriptor(std::initializer_list<wgpu::TextureView> colorAttachmentInfo,
                              wgpu::TextureView depthStencil = wgpu::TextureView());
    const ComboRenderPassDescriptor& operator=(
        const ComboRenderPassDescriptor& otherRenderPass);

    std::array<wgpu::RenderPassColorAttachmentDescriptor, kMaxColorAttachments>
    cColorAttachments;
    wgpu::RenderPassDepthStencilAttachmentDescriptor cDepthStencilAttachmentInfo;
};

ComboRenderPassDescriptor::ComboRenderPassDescriptor(
    std::initializer_list<wgpu::TextureView> colorAttachmentInfo,
    wgpu::TextureView depthStencil) {
    for (uint32_t i = 0; i < kMaxColorAttachments; ++i) {
        cColorAttachments[i].loadOp = wgpu::LoadOp::Clear;
        cColorAttachments[i].storeOp = wgpu::StoreOp::Store;
        cColorAttachments[i].clearColor = {0.0f, 0.0f, 0.0f, 0.0f};
    }

    cDepthStencilAttachmentInfo.clearDepth = 1.0f;
    cDepthStencilAttachmentInfo.clearStencil = 0;
    cDepthStencilAttachmentInfo.depthLoadOp = wgpu::LoadOp::Clear;
    cDepthStencilAttachmentInfo.depthStoreOp = wgpu::StoreOp::Store;
    cDepthStencilAttachmentInfo.stencilLoadOp = wgpu::LoadOp::Clear;
    cDepthStencilAttachmentInfo.stencilStoreOp = wgpu::StoreOp::Store;

    colorAttachmentCount = static_cast<uint32_t>(colorAttachmentInfo.size());
    uint32_t colorAttachmentIndex = 0;
    for (const wgpu::TextureView& colorAttachment : colorAttachmentInfo) {
        if (colorAttachment.Get() != nullptr) {
            cColorAttachments[colorAttachmentIndex].attachment = colorAttachment;
        }
        ++colorAttachmentIndex;
    }
    colorAttachments = cColorAttachments.data();

    if (depthStencil.Get() != nullptr) {
        cDepthStencilAttachmentInfo.attachment = depthStencil;
        depthStencilAttachment = &cDepthStencilAttachmentInfo;
    } else {
        depthStencilAttachment = nullptr;
    }
}

const ComboRenderPassDescriptor& ComboRenderPassDescriptor::operator=(
    const ComboRenderPassDescriptor& otherRenderPass) {
    cDepthStencilAttachmentInfo = otherRenderPass.cDepthStencilAttachmentInfo;
    cColorAttachments = otherRenderPass.cColorAttachments;
    colorAttachmentCount = otherRenderPass.colorAttachmentCount;

    colorAttachments = cColorAttachments.data();

    if (otherRenderPass.depthStencilAttachment != nullptr) {
        // Assign desc.depthStencilAttachment to this->depthStencilAttachmentInfo;
        depthStencilAttachment = &cDepthStencilAttachmentInfo;
    } else {
        depthStencilAttachment = nullptr;
    }

    return *this;
}

void Animation::init(GLFWwindow* window, int width, int height)
{
    Log(Log::Info) << "go me";

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
