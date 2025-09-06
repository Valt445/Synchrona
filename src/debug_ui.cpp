#include "debug_ui.h"
#include <imgui.h>
#include <vma/vk_mem_alloc.h>
#include <algorithm>
#include <string>
#include <cmath>
#include "engine.hpp"

struct DebugUIState {
    bool show_performance;
    bool show_scene_debug;
    bool show_renderer_stats;
    bool show_memory_stats;

    float frameTimes[100];
    int frameTimeOffset;
    float minFrameTime;
    float maxFrameTime;
    float avgFrameTime;

    // New: live FPS graph smoothing
    float fpsHistory[200];
    int fpsOffset;
};

DebugUIState g_debugUI = {
    .show_performance = true,
    .show_scene_debug = false,
    .show_renderer_stats = false,
    .show_memory_stats = false,
    .frameTimes = {0},
    .frameTimeOffset = 0,
    .minFrameTime = 0.0f,
    .maxFrameTime = 0.0f,
    .avgFrameTime = 0.0f,
    .fpsHistory = {0},
    .fpsOffset = 0
};

static Engine* s_engine = nullptr;

void debug_ui_init(Engine* engine) {
    s_engine = engine;

    ImGuiStyle* style = &ImGui::GetStyle();
    style->WindowRounding = 2.0f;
    style->ChildRounding = 2.0f;
    style->FrameRounding = 2.0f;
    style->GrabRounding = 2.0f;
    style->PopupRounding = 2.0f;
    style->ScrollbarRounding = 2.0f;

    for (int i = 0; i < 100; ++i) g_debugUI.frameTimes[i] = 16.67f;
    g_debugUI.minFrameTime = 16.67f;
    g_debugUI.maxFrameTime = 16.67f;
    g_debugUI.avgFrameTime = 16.67f;

    for (int i = 0; i < 200; ++i) g_debugUI.fpsHistory[i] = 60.0f;
}

void debug_ui_shutdown() {
    s_engine = nullptr;
}

void debug_ui_update_frame_stats(float deltaTime) {
    // Frame time
    g_debugUI.frameTimes[g_debugUI.frameTimeOffset] = deltaTime * 1000.0f;
    g_debugUI.frameTimeOffset = (g_debugUI.frameTimeOffset + 1) % 100;

    g_debugUI.minFrameTime = 1000.0f;
    g_debugUI.maxFrameTime = 0.0f;
    g_debugUI.avgFrameTime = 0.0f;

    for (int i = 0; i < 100; ++i) {
        g_debugUI.avgFrameTime += g_debugUI.frameTimes[i];
        g_debugUI.minFrameTime = std::min(g_debugUI.minFrameTime, g_debugUI.frameTimes[i]);
        g_debugUI.maxFrameTime = std::max(g_debugUI.maxFrameTime, g_debugUI.frameTimes[i]);
    }
    g_debugUI.avgFrameTime /= 100.0f;

    // FPS smoothing graph
    float fps = 1000.0f / g_debugUI.frameTimes[(g_debugUI.frameTimeOffset + 99) % 100];
    g_debugUI.fpsHistory[g_debugUI.fpsOffset] = fps;
    g_debugUI.fpsOffset = (g_debugUI.fpsOffset + 1) % 200;
}

void debug_ui_update(float deltaTime) {
    debug_ui_update_frame_stats(deltaTime);
}

void debug_ui_render() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Debug")) {
            ImGui::MenuItem("Performance", nullptr, &g_debugUI.show_performance);
            ImGui::MenuItem("Scene Debug", nullptr, &g_debugUI.show_scene_debug);
            ImGui::MenuItem("Renderer Stats", nullptr, &g_debugUI.show_renderer_stats);
            ImGui::MenuItem("Memory Stats", nullptr, &g_debugUI.show_memory_stats);
            ImGui::EndMenu();
        }
        ImGui::Text("| FPS: %.1f (%.2f ms)", 1000.0f / g_debugUI.avgFrameTime, g_debugUI.avgFrameTime);
        ImGui::EndMainMenuBar();
    }

    if (g_debugUI.show_performance) debug_ui_render_performance_window();
    if (g_debugUI.show_scene_debug) debug_ui_render_scene_debug_window();
    if (g_debugUI.show_renderer_stats) debug_ui_render_renderer_stats_window();
    if (g_debugUI.show_memory_stats) debug_ui_render_memory_stats_window();
}

void debug_ui_render_performance_window() {
    if (!ImGui::Begin("Performance", &g_debugUI.show_performance)) { ImGui::End(); return; }

    // Frame time graph
    ImGui::PlotLines("Frame Times (ms)", g_debugUI.frameTimes, 100, g_debugUI.frameTimeOffset,
                     nullptr, g_debugUI.minFrameTime, g_debugUI.maxFrameTime, ImVec2(0, 80));

    // FPS graph
    ImGui::PlotLines("FPS History", g_debugUI.fpsHistory, 200, g_debugUI.fpsOffset,
                     nullptr, 0.0f, 200.0f, ImVec2(0, 80));

    // Stats
    ImGui::Text("Frame Time: %.2f ms", g_debugUI.frameTimes[(g_debugUI.frameTimeOffset + 99) % 100]);
    ImGui::Text("Min: %.2f ms | Max: %.2f ms | Avg: %.2f ms", 
               g_debugUI.minFrameTime, g_debugUI.maxFrameTime, g_debugUI.avgFrameTime);
    ImGui::Text("FPS: %.1f", 1000.0f / g_debugUI.avgFrameTime);

    ImGui::Separator();
    if (s_engine) {
        ImGui::Text("Frame Number: %u", s_engine->frameNumber);
        ImGui::Text("Swapchain Images: %zu", s_engine->swapchainImages.size());

        // Live pipeline utilization bars
        ImGui::Separator();
        for (size_t i = 0; i < s_engine->backgroundEffects.size(); ++i) {
            ComputeEffect* e = &s_engine->backgroundEffects[i];
            ImGui::Text("%s", e->name);
            float usage = std::fmod(ImGui::GetTime() * 0.1f * (i+1), 1.0f); // demo wave
            ImGui::ProgressBar(usage, ImVec2(-1, 0), "GPU Load");
        }
    }

    ImGui::End();
}

void debug_ui_render_renderer_stats_window() {
    if (!ImGui::Begin("Renderer Stats", &g_debugUI.show_renderer_stats)) { ImGui::End(); return; }

    if (s_engine) {
        ImGui::Text("Vulkan Device: %p", s_engine->device);
        ImGui::Text("Graphics Queue: %p", s_engine->graphicsQueue);
        ImGui::Text("Draw Image: %dx%d", s_engine->drawImage.imageExtent.width,
                   s_engine->drawImage.imageExtent.height);

        ImGui::Separator();
        ImGui::Text("Background Effects: %zu", s_engine->backgroundEffects.size());
        for (size_t i = 0; i < s_engine->backgroundEffects.size(); ++i) {
            ImGui::Text("  %s: %p", s_engine->backgroundEffects[i].name,
                       s_engine->backgroundEffects[i].pipeline);
        }
    }

    ImGui::End();
}

void debug_ui_render_memory_stats_window() {
    if (!ImGui::Begin("Memory Stats", &g_debugUI.show_memory_stats)) { ImGui::End(); return; }

    if (s_engine) {
        ImGui::Text("Manual Memory Tracking:");
        ImGui::Text("Total: %.2f MB", s_engine->memoryStats.totalMemoryBytes / (1024.0f*1024.0f));
        ImGui::Text("Images: %.2f MB", s_engine->memoryStats.imageMemoryBytes / (1024.0f*1024.0f));
        ImGui::Text("Swapchain: %.2f MB", s_engine->memoryStats.swapchainMemoryBytes / (1024.0f*1024.0f));
        ImGui::Text("Buffers: %.2f MB", s_engine->memoryStats.bufferMemoryBytes / (1024.0f*1024.0f));

        if (s_engine->memoryStats.totalMemoryBytes > 0) {
            ImGui::ProgressBar((float)s_engine->memoryStats.imageMemoryBytes / s_engine->memoryStats.totalMemoryBytes, ImVec2(-1, 0), "Images");
            ImGui::ProgressBar((float)s_engine->memoryStats.swapchainMemoryBytes / s_engine->memoryStats.totalMemoryBytes, ImVec2(-1, 0), "Swapchain");
            ImGui::ProgressBar((float)s_engine->memoryStats.bufferMemoryBytes / s_engine->memoryStats.totalMemoryBytes, ImVec2(-1, 0), "Buffers");
        }
    } else {
        ImGui::Text("No engine available");
    }

    ImGui::End();
}


void debug_ui_render_scene_debug_window() {
    if (!ImGui::Begin("Scene Debug", &g_debugUI.show_scene_debug)) { ImGui::End(); return; }

    if (s_engine && !s_engine->backgroundEffects.empty()) {
    ImGui::Text("Background Effects:");

    // Show radio buttons to switch current effect
    for (size_t i = 0; i < s_engine->backgroundEffects.size(); ++i) {

        ImGui::PushID(i);
        
        bool is_active = (s_engine->currentBackgroundEffect == i);
        if (ImGui::RadioButton(s_engine->backgroundEffects[i].name, is_active)) {
            s_engine->currentBackgroundEffect = i; // switch shader pipeline
        }

        ImGui::PopID();
    }

    ImGui::Separator();

    // Edit parameters of the currently active effect
    ComputeEffect& effect = s_engine->backgroundEffects[s_engine->currentBackgroundEffect];
    if (ImGui::TreeNode(effect.name)) {
        ImGui::InputFloat4("Data 1", &effect.data.data1.x);
        ImGui::InputFloat4("Data 2", &effect.data.data2.x);
        ImGui::InputFloat4("Data 3", &effect.data.data3.x);
        ImGui::InputFloat4("Data 4", &effect.data.data4.x);

        // Live demo preview
        float wave = std::sin((float)ImGui::GetTime()) * 0.5f + 0.5f;
        ImGui::ProgressBar(wave, ImVec2(-1,0), "Shader Param");

        ImGui::TreePop();
    }
}

    ImGui::End();
}
