#include "render/Animation.h"
#include <GLFW/glfw3.h>
#include <args/Args.h>
#include <args/Parser.h>
#include <event/Loop.h>
#include <log/Log.h>
#include <thread>

using namespace reckoning;
using namespace reckoning::log;
using namespace std::chrono_literals;

// #define ANIMATION_USE_THREAD

static void PrintGLFWError(int code, const char* message) {
    Log(Log::Info) << "GLFW error: " << code << " - " << message;
}

#ifdef ANIMATION_USE_THREAD
static void animationThread(GLFWwindow* window, int width, int height)
{
    // glfwMakeContextCurrent(window);

    Animation animation;
    animation.init(window, width, height);

    std::shared_ptr<event::Loop> loop = event::Loop::create();

    for (;;) {
        animation.frame();
        loop->execute(16ms);
        if (loop->stopped())
            break;
    }

    // not thread safe?
    glfwSetWindowShouldClose(window, 1);
}
#endif

int main(int argc, char** argv)
{
    auto args = args::Parser::parse(argc, argv);

    // default configs
    Log::Level level = Log::Debug;
    int width = 1280;
    int height = 720;

    if (args.has<int>("width"))
        width = args.value<int>("width");
    if (args.has<int>("height"))
        height = args.value<int>("height");
    if (args.has<std::string>("level")) {
        const auto& slevel = args.value<std::string>("level");
        if (slevel == "debug")
            level = Log::Debug;
        else if (slevel == "info")
            level = Log::Info;
        else if (slevel == "warn")
            level = Log::Warn;
        else if (slevel == "error")
            level = Log::Error;
        else if (slevel == "fatal")
            level = Log::Fatal;
    }

    Log::initialize(level);

    glfwSetErrorCallback(PrintGLFWError);
    if (!glfwInit()) {
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow* window = glfwCreateWindow(width, height, "Dawn window", nullptr, nullptr);

    // prepare for moving the glfw context to the animation thread
    // glfwMakeContextCurrent(nullptr);

#ifdef ANIMATION_USE_THREAD
    // make the animation thread
    std::thread thread = std::thread(animationThread, window, width, height);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
    }

    thread.join();
#else
    std::shared_ptr<event::Loop> loop = event::Loop::create();

    Animation animation;
    animation.init(window, width, height);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        animation.frame();
        loop->execute(16ms);
        if (loop->stopped())
            break;
    }

    loop.reset();
#endif

    return 0;
}
