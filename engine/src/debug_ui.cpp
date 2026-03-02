#include "debug_ui.h"
#include "engine.h"   // full engine definition for live stats access

#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <cmath>

DebugUIState g_debugUI;
static Engine* s_engine = nullptr;

// ─── Init / Shutdown ─────────────────────────────────────────────────────────
void debug_ui_init(Engine* e)
{
    s_engine = e;

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 12.0f;
    style.GrabRounding = 4.0f;
    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 5);
    style.WindowPadding = ImVec2(10, 10);

    ImGui::StyleColorsDark();

    // Make the dark theme slightly nicer
    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.12f, 0.92f);
    colors[ImGuiCol_Header] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.55f);
}

void debug_ui_shutdown()
{
    s_engine = nullptr;
    g_debugUI.logMessages.clear();
}

// ─── Update ──────────────────────────────────────────────────────────────────
void debug_ui_update(float deltaTime)
{
    float msec = deltaTime * 1000.0f;

    g_debugUI.frameTimes.erase(g_debugUI.frameTimes.begin());
    g_debugUI.frameTimes.push_back(msec);

    g_debugUI.minFrameTime = *std::min_element(
        g_debugUI.frameTimes.begin(), g_debugUI.frameTimes.end());
    g_debugUI.maxFrameTime = *std::max_element(
        g_debugUI.frameTimes.begin(), g_debugUI.frameTimes.end());

    g_debugUI.avgFrameTime = 0.0f;
    for (float t : g_debugUI.frameTimes) g_debugUI.avgFrameTime += t;
    g_debugUI.avgFrameTime /= (float)g_debugUI.frameTimes.size();

    float fps = (msec > 0.01f) ? 1000.0f / msec : 0.0f;
    g_debugUI.fpsHistory.erase(g_debugUI.fpsHistory.begin());
    g_debugUI.fpsHistory.push_back(fps);
}

// ─── Logging ──────────────────────────────────────────────────────────────────
void debug_ui_log(const char* fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    g_debugUI.logMessages.emplace_back(buf);
    if (g_debugUI.logMessages.size() > 800)
        g_debugUI.logMessages.erase(g_debugUI.logMessages.begin());
}

void debug_ui_log(const std::string& msg)
{
    g_debugUI.logMessages.push_back(msg);
    if (g_debugUI.logMessages.size() > 800)
        g_debugUI.logMessages.erase(g_debugUI.logMessages.begin());
}

// ─── Main render ─────────────────────────────────────────────────────────────
void debug_ui_render(Engine* e)
{
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Debug")) {
            ImGui::MenuItem("Performance", nullptr, &g_debugUI.show_performance);
            ImGui::MenuItem("Scene", nullptr, &g_debugUI.show_scene_debug);
            ImGui::MenuItem("Renderer Stats", nullptr, &g_debugUI.show_renderer_stats);
            ImGui::MenuItem("Memory Stats", nullptr, &g_debugUI.show_memory_stats);
            ImGui::MenuItem("Log Console", nullptr, &g_debugUI.show_log_console);
            ImGui::MenuItem("Input Debug", nullptr, &g_debugUI.show_input_debug);
            ImGui::Separator();
            ImGui::MenuItem("Background FX", nullptr, &g_debugUI.show_background_ctrl);
            ImGui::Separator();
            ImGui::MenuItem("Style Editor", nullptr, &g_debugUI.show_style_editor);
            ImGui::MenuItem("ImGui Demo", nullptr, &g_debugUI.show_imgui_demo);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            ImGui::Checkbox("Dark Mode", &g_debugUI.darkMode);
            ImGui::SliderFloat("UI Scale", &g_debugUI.uiScale, 0.5f, 2.0f);
            ImGui::EndMenu();
        }

        // Live FPS in menu bar
        float fps = (g_debugUI.avgFrameTime > 0.01f)
            ? 1000.0f / g_debugUI.avgFrameTime : 0.0f;
        ImVec4 col = fps >= 55 ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
            : fps >= 30 ? ImVec4(1.0f, 1.0f, 0.1f, 1.0f)
            : ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::Text("  %.0f FPS  (%.2f ms)", fps, g_debugUI.avgFrameTime);
        ImGui::PopStyleColor();

        // Draw calls always visible in menu bar
        if (e) {
            ImGui::Separator();
            ImGui::TextDisabled("  Draws: %u  Tris: %s",
                e->lastDrawCalls,
                [](uint32_t n) -> const char* {
                    static char buf[32];
                    if (n >= 1000000) snprintf(buf, sizeof(buf), "%.1fM", n / 1000000.0f);
                    else if (n >= 1000) snprintf(buf, sizeof(buf), "%.1fK", n / 1000.0f);
                    else snprintf(buf, sizeof(buf), "%u", n);
                    return buf;
                }(e->lastTriangles));
        }

        ImGui::EndMainMenuBar();
    }

    if (g_debugUI.show_performance)     debug_ui_render_performance_window();
    if (g_debugUI.show_scene_debug)     debug_ui_render_scene_debug_window(e);
    if (g_debugUI.show_renderer_stats)  debug_ui_render_renderer_stats_window(e);
    if (g_debugUI.show_memory_stats)    debug_ui_render_memory_stats_window(e);
    if (g_debugUI.show_log_console)     debug_ui_render_log_console_window();
    if (g_debugUI.show_input_debug)     debug_ui_render_input_debug_window();
    if (g_debugUI.show_background_ctrl) debug_ui_render_background_ctrl(e);
    if (g_debugUI.show_style_editor)    debug_ui_render_style_editor();
    if (g_debugUI.show_imgui_demo)      debug_ui_render_imgui_demo();
}

// ─── Performance panel ───────────────────────────────────────────────────────
void debug_ui_render_performance_window()
{
    ImGui::Begin("Performance", &g_debugUI.show_performance,
        ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::Text("Avg: %.2f ms   Min: %.2f   Max: %.2f",
        g_debugUI.avgFrameTime, g_debugUI.minFrameTime, g_debugUI.maxFrameTime);

    float maxScale = std::max(g_debugUI.maxFrameTime * 1.3f, 5.0f);
    ImGui::PlotLines("##frametimes", g_debugUI.frameTimes.data(),
        (int)g_debugUI.frameTimes.size(), 0, "Frame Time (ms)", 0.0f, maxScale,
        ImVec2(400, 80));
    ImGui::PlotHistogram("##fps", g_debugUI.fpsHistory.data(),
        (int)g_debugUI.fpsHistory.size(), 0, "FPS", 0.0f, 165.0f,
        ImVec2(400, 60));

    ImGui::End();
}

// ─── Scene debug panel ───────────────────────────────────────────────────────
void debug_ui_render_scene_debug_window(Engine* e)
{
    ImGui::Begin("Scene", &g_debugUI.show_scene_debug);

    if (!e) { ImGui::Text("No engine"); ImGui::End(); return; }

    ImGui::Text("Mesh assets loaded: %zu", e->testMeshes.size());
    ImGui::Text("Bindless textures:  %u", e->nextBindlessTextureIndex);
    ImGui::Separator();

    // Per-mesh details
    if (ImGui::CollapsingHeader("Mesh List", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (size_t i = 0; i < e->testMeshes.size(); ++i) {
            auto& m = e->testMeshes[i];
            ImGui::PushID((int)i);

            bool open = ImGui::TreeNode("##mesh", "[%zu] %s  (%zu surfaces)",
                i, m->name.c_str(), m->surfaces.size());
            if (open) {
                // World transform — show translation component
                glm::vec3 pos = glm::vec3(m->worldTransform[3]);
                ImGui::Text("World pos: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z);

                uint32_t totalTris = 0;
                for (auto& s : m->surfaces) totalTris += s.count / 3;
                ImGui::Text("Triangles: %u", totalTris);

                // Surface table
                if (ImGui::BeginTable("##surfs", 6,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_ScrollY, ImVec2(0, 100))) {
                    ImGui::TableSetupColumn("Surf");
                    ImGui::TableSetupColumn("Tris");
                    ImGui::TableSetupColumn("Alb");
                    ImGui::TableSetupColumn("Nrm");
                    ImGui::TableSetupColumn("MR");
                    ImGui::TableSetupColumn("AO");
                    ImGui::TableHeadersRow();

                    for (size_t si = 0; si < m->surfaces.size(); ++si) {
                        auto& s = m->surfaces[si];
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::Text("%zu", si);
                        ImGui::TableNextColumn(); ImGui::Text("%u", s.count / 3);
                        ImGui::TableNextColumn(); ImGui::Text("%u", s.albedoIndex);
                        ImGui::TableNextColumn(); ImGui::Text("%u", s.normalIndex);
                        ImGui::TableNextColumn(); ImGui::Text("%u", s.metallicRoughnessIndex);
                        ImGui::TableNextColumn(); ImGui::Text("%u", s.aoIndex);
                    }
                    ImGui::EndTable();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    // Camera info
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Camera")) {
        glm::vec3 p = e->mainCamera.position;
        ImGui::Text("Position: (%.2f, %.2f, %.2f)", p.x, p.y, p.z);
        ImGui::Text("Yaw: %.1f   Pitch: %.1f",
            e->mainCamera.yaw, e->mainCamera.pitch);
    }

    ImGui::End();
}

// ─── Renderer stats panel ────────────────────────────────────────────────────
void debug_ui_render_renderer_stats_window(Engine* e)
{
    ImGui::Begin("Renderer Stats", &g_debugUI.show_renderer_stats);

    if (!e) { ImGui::Text("No engine"); ImGui::End(); return; }

    ImGui::Text("Draw calls:       %u", e->lastDrawCalls);

    // Format triangles nicely
    uint32_t t = e->lastTriangles;
    if (t >= 1000000)
        ImGui::Text("Triangles:        %.2fM", t / 1000000.0f);
    else if (t >= 1000)
        ImGui::Text("Triangles:        %.1fK", t / 1000.0f);
    else
        ImGui::Text("Triangles:        %u", t);

    ImGui::Text("Textures (bindless): %u / 4096", e->nextBindlessTextureIndex);
    ImGui::Text("Mesh assets:         %zu", e->testMeshes.size());
    ImGui::Text("Frame #:             %d", e->frameNumber);
    ImGui::Text("Frame overlap:       %d", FRAME_OVERLAP);

    ImGui::Separator();
    ImGui::Text("Draw image:   %ux%u  R16G16B16A16_SFLOAT",
        e->drawExtent.width, e->drawExtent.height);
    ImGui::Text("Swapchain:    %ux%u",
        e->swapchainExtent.width, e->swapchainExtent.height);

    ImGui::End();
}

// ─── Memory stats panel ──────────────────────────────────────────────────────
void debug_ui_render_memory_stats_window(Engine* e)
{
    ImGui::Begin("Memory Stats", &g_debugUI.show_memory_stats);

    if (!e) { ImGui::Text("No engine"); ImGui::End(); return; }

    auto mb = [](size_t bytes) { return bytes / (1024.0f * 1024.0f); };

    ImGui::Text("Swapchain VRAM: %.1f MB", mb(e->memoryStats.swapchainMemoryBytes));
    ImGui::Text("Image VRAM:     %.1f MB", mb(e->memoryStats.imageMemoryBytes));
    ImGui::Text("Buffer VRAM:    %.1f MB", mb(e->memoryStats.bufferMemoryBytes));
    ImGui::Separator();

    // VMA live stats
    if (e->allocator) {
        VmaTotalStatistics stats{};
        vmaCalculateStatistics(e->allocator, &stats);
        float usedMB = stats.total.statistics.allocationBytes / (1024.0f * 1024.0f);
        float blockMB = stats.total.statistics.blockBytes / (1024.0f * 1024.0f);
        ImGui::Text("VMA allocated: %.1f MB", usedMB);
        ImGui::Text("VMA reserved:  %.1f MB", blockMB);
        ImGui::Text("Allocations:   %u", stats.total.statistics.allocationCount);
    }

    ImGui::End();
}

// ─── Background / compute shader switcher ────────────────────────────────────
void debug_ui_render_background_ctrl(Engine* e)
{
    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_FirstUseEver);
    ImGui::Begin("Background FX", &g_debugUI.show_background_ctrl,
        ImGuiWindowFlags_AlwaysAutoResize);

    if (!e || e->backgroundEffects.empty()) {
        ImGui::Text("No background effects loaded");
        ImGui::End(); return;
    }

    // Effect selector
    ImGui::Text("Effect:");
    for (int i = 0; i < (int)e->backgroundEffects.size(); ++i) {
        bool selected = (e->currentBackgroundEffect == i);
        if (ImGui::RadioButton(e->backgroundEffects[i].name, selected))
            e->currentBackgroundEffect = i;
    }

    ImGui::Separator();

    // Live parameter sliders for the current effect
    ScenePushConstants& data = e->backgroundEffects[e->currentBackgroundEffect].effectData;
    const char* effectName = e->backgroundEffects[e->currentBackgroundEffect].name;

    if (std::string(effectName) == "gradient") {
        ImGui::ColorEdit3("Color A", &data.data1.x);
        ImGui::ColorEdit3("Color B", &data.data2.x);
    }
    else if (std::string(effectName) == "sky") {
        ImGui::ColorEdit3("Sky Color", &data.data1.x);
        ImGui::SliderFloat("Density", &data.data1.w, 0.0f, 1.0f);
    }
    else {
        // Generic sliders for unknown effects
        ImGui::SliderFloat4("data1", &data.data1.x, 0.0f, 1.0f);
        ImGui::SliderFloat4("data2", &data.data2.x, 0.0f, 1.0f);
        ImGui::SliderFloat4("data3", &data.data3.x, 0.0f, 1.0f);
        ImGui::SliderFloat4("data4", &data.data4.x, 0.0f, 1.0f);
    }

    ImGui::End();
}

// ─── Log console ─────────────────────────────────────────────────────────────
void debug_ui_render_log_console_window()
{
    ImGui::Begin("Log Console", &g_debugUI.show_log_console);

    if (ImGui::Button("Clear")) g_debugUI.logMessages.clear();
    ImGui::SameLine();
    ImGui::Checkbox("Auto Scroll", &g_debugUI.autoScrollLog);
    g_debugUI.logFilter.Draw("Filter", 200);

    ImGui::Separator();
    ImGui::BeginChild("##logscroll", ImVec2(0, 0), false,
        ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& msg : g_debugUI.logMessages)
        if (g_debugUI.logFilter.PassFilter(msg.c_str()))
            ImGui::TextUnformatted(msg.c_str());
    if (g_debugUI.autoScrollLog)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}

// ─── Input debug ─────────────────────────────────────────────────────────────
void debug_ui_render_input_debug_window()
{
    ImGui::Begin("Input Debug", &g_debugUI.show_input_debug);
    ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("Mouse Pos:   (%.1f, %.1f)", io.MousePos.x, io.MousePos.y);
    ImGui::Text("Mouse Delta: (%.1f, %.1f)", io.MouseDelta.x, io.MouseDelta.y);
    if (ImGui::CollapsingHeader("Mouse Buttons")) {
        for (int i = 0; i < 5; i++)
            if (ImGui::IsMouseDown(i))
                ImGui::Text("Button %d DOWN", i);
    }
    ImGui::End();
}

// ─── Style editor ────────────────────────────────────────────────────────────
void debug_ui_render_style_editor()
{
    ImGui::Begin("Style Editor", &g_debugUI.show_style_editor);
    ImGui::ShowStyleEditor();
    ImGui::End();
}

// ─── ImGui demo ──────────────────────────────────────────────────────────────
void debug_ui_render_imgui_demo()
{
    ImGui::ShowDemoWindow(&g_debugUI.show_imgui_demo);
}