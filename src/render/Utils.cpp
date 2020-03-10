#include "Utils.h"
#include <log/Log.h>

using namespace reckoning;
using namespace reckoning::log;

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

ComboRenderBundleEncoderDescriptor::ComboRenderBundleEncoderDescriptor() {
    wgpu::RenderBundleEncoderDescriptor* descriptor = this;

    descriptor->colorFormatsCount = 0;
    descriptor->colorFormats = &cColorFormats[0];
}
