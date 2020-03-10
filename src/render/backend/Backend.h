#ifndef BACKEND_H
#define BACKEND_H

#include <dawn/webgpu.h>
#include <cstdint>
#include <memory>

typedef struct GLFWwindow GLFWwindow;

class BackendBinding {
public:
    virtual ~BackendBinding() = default;

    virtual uint64_t GetSwapChainImplementation() = 0;
    virtual WGPUTextureFormat GetPreferredSwapChainTextureFormat() = 0;

protected:
    BackendBinding(GLFWwindow* window, WGPUDevice device)
        : mWindow(window), mDevice(device)
    {
    }


    GLFWwindow* mWindow = nullptr;
    WGPUDevice mDevice = nullptr;
};

std::shared_ptr<BackendBinding> makeBackendBinding(GLFWwindow* window, WGPUDevice device);

#endif // BACKEND_H
