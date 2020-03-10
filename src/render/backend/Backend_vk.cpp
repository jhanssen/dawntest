#include "Backend.h"
#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <dawn_native/VulkanBackend.h>
#include <cassert>

class VulkanBinding : public BackendBinding {
public:
    VulkanBinding(GLFWwindow* window, WGPUDevice device) : BackendBinding(window, device) {
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

std::shared_ptr<BackendBinding> makeBackendBinding(GLFWwindow* window, WGPUDevice device)
{
    return std::make_shared<VulkanBinding>(window, device);
}
