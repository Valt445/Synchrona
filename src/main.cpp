#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "engine.hpp"
#include "debug_ui.h"
#include <imgui.h>
#include <thread>
#include <chrono>

int main() {
    Engine engineInstance;
    engine = &engineInstance; // set global pointer
    init(engine, 1280, 720); // Use a standard initial size (e.g., 1280x720)

    bool stop_rendering = false;

    // NEW: Linux/HiDPI hints—full pixel extents, no scaling drift
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);  // CHANGED: Auto-scale to DPI (Pop!_OS GNOME fix)
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);  // If Wayland-like scaling; harmless on X11

    while (!glfwWindowShouldClose(engine->window)) {
        glfwPollEvents();

        // Pause rendering if minimized
        stop_rendering = glfwGetWindowAttrib(engine->window, GLFW_ICONIFIED) != 0;
        if (stop_rendering) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Handle rendering (includes resize logic internally)
        engine_draw_frame(engine);
    }

    // Wait for GPU to finish before destroying anything
    vkDeviceWaitIdle(engine->device);

    // Clean up engine (includes ImGui shutdown)
    engine_cleanup(engine);

    return 0;
}
