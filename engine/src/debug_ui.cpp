#include "debug_ui.h"
#include <imgui.h>
#include <vk_mem_alloc.h>
#include <algorithm>
#include <string>
#include <cmath>
#include "engine.h"

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

    float fpsHistory[200];
    int fpsOffset;
};

DebugUIState g_debugUI = {
    .show_performance = true,
    .show_scene_debug = true,
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

void debug_ui_init(Engine* e) {
    s_engine = e;

    ImGuiStyle* style = &ImGui::GetStyle();
    style->WindowRounding = 4.0f;
    style->FrameRounding = 4.0f;

    for (int i = 0; i < 100; ++i) g_debugUI.frameTimes[i] = 16.67f;
    for (int i = 0; i < 200; ++i) g_debugUI.fpsHistory[i] = 60.0f;
}

void debug_ui_shutdown() {
    s_engine = nullptr;
}

void debug_ui_update_frame_stats(float deltaTime) {
    float msec = deltaTime * 1000.0f;
    g_debugUI.frameTimes[g_debugUI.frameTimeOffset] = msec;
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

    float fps = 1000.0f / (msec > 0.01f ? msec : 0.01f);
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
        ImGui::Text("| FPS: %.1f", 1000.0f / g_debugUI.avgFrameTime);
        ImGui::EndMainMenuBar();
    }

    if (g_debugUI.show_performance) debug_ui_render_performance_window();
    if (g_debugUI.show_scene_debug) debug_ui_render_scene_debug_window();
    if (g_debugUI.show_renderer_stats) debug_ui_render_renderer_stats_window();
    if (g_debugUI.show_memory_stats) debug_ui_render_memory_stats_window();
}

void debug_ui_render_performance_window() {
    if (!ImGui::Begin("Performance", &g_debugUI.show_performance)) { ImGui::End(); return; }

    ImGui::PlotLines("ms", g_debugUI.frameTimes, 100, g_debugUI.frameTimeOffset, nullptr, 0.0f, 33.0f, ImVec2(0, 80));
    ImGui::Text("Avg: %.2f ms (%.1f FPS)", g_debugUI.avgFrameTime, 1000.0f / g_debugUI.avgFrameTime);

    ImGui::End();
}

void debug_ui_render_scene_debug_window() {
    if (!ImGui::Begin("Scene Debug", &g_debugUI.show_scene_debug)) { ImGui::End(); return; }

    if (s_engine) {
        if (ImGui::CollapsingHeader("Background Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (size_t i = 0; i < s_engine->backgroundEffects.size(); ++i) {
                if (ImGui::RadioButton(s_engine->backgroundEffects[i].name, s_engine->currentBackgroundEffect == (int)i)) {
                    s_engine->currentBackgroundEffect = (int)i;
                }
            }

            ImGui::Separator();
            ComputeEffect& curr = s_engine->backgroundEffects[s_engine->currentBackgroundEffect];

            // FIXED: Using .effectData instead of .data
            ImGui::InputFloat4("Data 1", &curr.effectData.data1.x);
            ImGui::InputFloat4("Data 2", &curr.effectData.data2.x);
            ImGui::InputFloat4("Data 3", &curr.effectData.data3.x);
            ImGui::InputFloat4("Data 4", &curr.effectData.data4.x);
        }

        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Mesh Inspector", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (auto& mesh : s_engine->testMeshes) {
                if (ImGui::TreeNode(mesh->name.c_str())) {
                    for (size_t i = 0; i < mesh->surfaces.size(); i++) {
                        ImGui::Text("Surface %zu | MaterialIdx: %u | Count: %u",
                            i, mesh->surfaces[i].materialIdx, mesh->surfaces[i].count);
                    }
                    ImGui::TreePop();
                }
            }
        }
    }
    ImGui::End();
}

void debug_ui_render_renderer_stats_window() {
    if (!ImGui::Begin("Renderer Stats", &g_debugUI.show_renderer_stats)) { ImGui::End(); return; }
    if (s_engine) {
        ImGui::Text("Draw Res: %dx%d", s_engine->drawImage.imageExtent.width, s_engine->drawImage.imageExtent.height);
        ImGui::Text("Meshes in Scene: %zu", s_engine->testMeshes.size());
    }
    ImGui::End();
}

void debug_ui_render_memory_stats_window() {
    if (!ImGui::Begin("Memory Stats", &g_debugUI.show_memory_stats)) { ImGui::End(); return; }
    if (s_engine) {
        float totalMB = s_engine->memoryStats.totalMemoryBytes / (1024.f * 1024.f);
        ImGui::Text("Total Allocated: %.2f MB", totalMB);
    }
    ImGui::End();
}