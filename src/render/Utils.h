#ifndef UTILS_H
#define UTILS_H

#include "Constants.h"
#include <dawn/webgpu_cpp.h>
#include <shaderc/shaderc.hpp>
#include <array>
#include <cstdint>

enum class SingleShaderStage { Vertex, Fragment, Compute };

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

wgpu::Buffer CreateBufferFromData(const wgpu::Device& device,
                                  const void* data,
                                  uint64_t size,
                                  wgpu::BufferUsage usage);

template <typename T>
wgpu::Buffer CreateBufferFromData(const wgpu::Device& device,
                                  wgpu::BufferUsage usage,
                                  std::initializer_list<T> data) {
    return CreateBufferFromData(device, data.begin(), uint32_t(sizeof(T) * data.size()), usage);
}

wgpu::SamplerDescriptor GetDefaultSamplerDescriptor();

wgpu::BufferCopyView CreateBufferCopyView(wgpu::Buffer buffer,
                                          uint64_t offset,
                                          uint32_t rowPitch,
                                          uint32_t imageHeight);

wgpu::TextureCopyView CreateTextureCopyView(wgpu::Texture texture,
                                            uint32_t mipLevel,
                                            uint32_t arrayLayer,
                                            wgpu::Origin3D origin);


shaderc_shader_kind ShadercShaderKind(SingleShaderStage stage);

wgpu::ShaderModule CreateShaderModuleFromResult(const wgpu::Device& device,
                                                const shaderc::SpvCompilationResult& result);

wgpu::ShaderModule CreateShaderModule(const wgpu::Device& device,
                                      SingleShaderStage stage,
                                      const std::string& source);

wgpu::BindGroupLayout MakeBindGroupLayout(const wgpu::Device& device,
                                          std::initializer_list<wgpu::BindGroupLayoutBinding> bindingsInitializer);

wgpu::TextureView CreateDefaultDepthStencilView(const wgpu::Device& device, uint32_t width, uint32_t height);

wgpu::PipelineLayout MakeBasicPipelineLayout(const wgpu::Device& device,
                                             const wgpu::BindGroupLayout* bindGroupLayout);

wgpu::BindGroup MakeBindGroup(const wgpu::Device& device,
                              const wgpu::BindGroupLayout& layout,
                              std::initializer_list<BindingInitializationHelper> bindingsInitializer);


#endif // UTILS_H
