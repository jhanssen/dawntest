#include "Animation.h"
#include <log/Log.h>
#include <dawn/webgpu_cpp.h>
#include <dawn_native/DawnNative.h>
#include <memory>

using namespace reckoning;
using namespace reckoning::log;

void Animation::run()
{
    Log(Log::Info) << "go me";

    auto instance = std::make_unique<dawn_native::Instance>();

}
