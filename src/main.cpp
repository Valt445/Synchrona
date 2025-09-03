#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "engine.hpp"
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

        // Start ImGui frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Debug window
        ImGui::Begin("Debug Window");
        ImGui::Text("Hello, Vulkan and ImGui!");
        ImGui::Text("Frame Time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        if (ImGui::Button("Click Me")) {
            ImGui::Text("Button clicked!");
        }
        ImGui::End();

        // Render ImGui
        ImGui::Render();
        engine_draw_frame(engine);
    }

    // Wait for GPU to finish before destroying anything
    vkDeviceWaitIdle(engine->device);

    // Only clean up engine — do NOT manually call ImGui shutdown if engine_cleanup already handles it
    engine_cleanup(engine);

    return 0;
}
