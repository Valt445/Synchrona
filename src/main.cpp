#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "engine.hpp"
#include <imgui.h>
#include <stdio.h>
#include <thread>
#include <chrono>

int main() {
    // Initialize engine
    Engine engine;
    init(&engine, 1234, 1234);
      // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable keyboard controls


    bool stop_rendering = false;

    while (!glfwWindowShouldClose(engine.window)) {
        // Poll events
        glfwPollEvents();

        // Handle window minimization
        if (glfwGetWindowAttrib(engine.window, GLFW_ICONIFIED)) {
            stop_rendering = true;
        } else {
            stop_rendering = false;
        }

        // Skip rendering if minimized
        if (stop_rendering) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Start new ImGui frame
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

        // Draw frame
        engine_draw_frame(&engine);
    }

    // Cleanup
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    engine_cleanup(&engine);

    return 0;
}
