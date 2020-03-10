#include "Backend.h"
#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

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

std::shared_ptr<AbstractBackendBinding> makeBackendBinding(GLFWwindow* window, WGPUDevice device)
{
    return std::make_shared<BackendBinding>(window, device);
}
