#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "engine.hpp"
#include "debug_ui.h"  // Make sure this is included
#include <imgui.h>
#include <thread>
#include <chrono>

int main() {
    Engine engineInstance;
    engine = &engineInstance; // set global pointer
    init(engine, 1234, 1234); // initializes engine + ImGui

    bool stop_rendering = false;

    while (!glfwWindowShouldClose(engine->window)) {
        glfwPollEvents();

        // pause rendering if minimized
        stop_rendering = glfwGetWindowAttrib(engine->window, GLFW_ICONIFIED) != 0;
        if (stop_rendering) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

  
        // This will handle ImGui rendering internally
        engine_draw_frame(engine);
    }

    // Wait for GPU to finish before destroying anything
    vkDeviceWaitIdle(engine->device);

    // Clean up engine (includes ImGui shutdown)
    engine_cleanup(engine);

    return 0;
}
