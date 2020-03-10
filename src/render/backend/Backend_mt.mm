#include "Backend.h"
#include <cassert>
#define DAWN_ENABLE_BACKEND_METAL
#include <dawn/dawn_wsi.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <QuartzCore/CAMetalLayer.h>

template <typename T>
DawnSwapChainImplementation CreateSwapChainImplementation(T* swapChain) {
    DawnSwapChainImplementation impl = {};
    impl.userData = swapChain;
    impl.Init = [](void* userData, void* wsiContext) {
        auto* ctx = static_cast<typename T::WSIContext*>(wsiContext);
        reinterpret_cast<T*>(userData)->Init(ctx);
    };
    impl.Destroy = [](void* userData) { delete reinterpret_cast<T*>(userData); };
    impl.Configure = [](void* userData, WGPUTextureFormat format, WGPUTextureUsage allowedUsage,
                        uint32_t width, uint32_t height) {
        return static_cast<T*>(userData)->Configure(format, allowedUsage, width, height);
    };
    impl.GetNextTexture = [](void* userData, DawnSwapChainNextTexture* nextTexture) {
        return static_cast<T*>(userData)->GetNextTexture(nextTexture);
    };
    impl.Present = [](void* userData) { return static_cast<T*>(userData)->Present(); };
    return impl;
}

class SwapChainImplMTL
{
public:
    using WSIContext = DawnWSIContextMetal;

    SwapChainImplMTL(id nsWindow)
        : mNsWindow(nsWindow)
    {
    }

    ~SwapChainImplMTL() {
        [mCurrentTexture release];
        [mCurrentDrawable release];
    }

    void Init(DawnWSIContextMetal* ctx) {
        mMtlDevice = ctx->device;
        mCommandQueue = ctx->queue;
    }

    DawnSwapChainError Configure(WGPUTextureFormat format,
                                 WGPUTextureUsage usage,
                                 uint32_t width,
                                 uint32_t height) {
        if (format != WGPUTextureFormat_BGRA8Unorm) {
            return "unsupported format";
        }
        assert(width > 0);
        assert(height > 0);

        NSView* contentView = [mNsWindow contentView];
        [contentView setWantsLayer:YES];

        CGSize size = {};
        size.width = width;
        size.height = height;

        mLayer = [CAMetalLayer layer];
        [mLayer setDevice:mMtlDevice];
        [mLayer setPixelFormat:MTLPixelFormatBGRA8Unorm];
        [mLayer setDrawableSize:size];

        constexpr uint32_t kFramebufferOnlyTextureUsages = WGPUTextureUsage_OutputAttachment | WGPUTextureUsage_Present;
        bool hasOnlyFramebufferUsages = !(usage & (~kFramebufferOnlyTextureUsages));
        if (hasOnlyFramebufferUsages) {
            [mLayer setFramebufferOnly:YES];
        }

        [contentView setLayer:mLayer];

        return DAWN_SWAP_CHAIN_NO_ERROR;
    }

    DawnSwapChainError GetNextTexture(DawnSwapChainNextTexture* nextTexture) {
        [mCurrentDrawable release];
        mCurrentDrawable = [mLayer nextDrawable];
        [mCurrentDrawable retain];

        [mCurrentTexture release];
        mCurrentTexture = mCurrentDrawable.texture;
        [mCurrentTexture retain];

        nextTexture->texture.ptr = reinterpret_cast<void*>(mCurrentTexture);

        return DAWN_SWAP_CHAIN_NO_ERROR;
    }

    DawnSwapChainError Present() {
        id<MTLCommandBuffer> commandBuffer = [mCommandQueue commandBuffer];
        [commandBuffer presentDrawable:mCurrentDrawable];
        [commandBuffer commit];

        return DAWN_SWAP_CHAIN_NO_ERROR;
    }

private:
    id mNsWindow = nil;
    id<MTLDevice> mMtlDevice = nil;
    id<MTLCommandQueue> mCommandQueue = nil;

    CAMetalLayer* mLayer = nullptr;
    id<CAMetalDrawable> mCurrentDrawable = nil;
    id<MTLTexture> mCurrentTexture = nil;
};

class MetalBinding : public AbstractBackendBinding {
public:
    MetalBinding(GLFWwindow* window, WGPUDevice device)
        : AbstractBackendBinding(window, device)
    {
    }

    uint64_t GetSwapChainImplementation() override {
        if (mSwapchainImpl.userData == nullptr) {
            mSwapchainImpl = CreateSwapChainImplementation(
                new SwapChainImplMTL(glfwGetCocoaWindow(mWindow)));
        }
        return reinterpret_cast<uint64_t>(&mSwapchainImpl);
    }

    WGPUTextureFormat GetPreferredSwapChainTextureFormat() override {
        return WGPUTextureFormat_BGRA8Unorm;
    }

private:
    DawnSwapChainImplementation mSwapchainImpl = {};
};

std::shared_ptr<AbstractBackendBinding> makeBackendBinding(GLFWwindow* window, WGPUDevice device)
{
    return std::make_shared<MetalBinding>(window, device);
}
