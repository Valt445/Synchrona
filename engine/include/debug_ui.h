#pragma once

#include <vector>
#include <string>
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

struct Engine;  // forward declare — debug_ui.h must not include engine.h (circular)

struct DebugUIState {
    // ── Windows ───────────────────────────────────────────────────────────
    bool show_performance = false;
    bool show_scene_debug = false;
    bool show_renderer_stats = false;
    bool show_memory_stats = false;
    bool show_log_console = false;
    bool show_input_debug = false;
    bool show_style_editor = false;
    bool show_imgui_demo = false;
    bool show_background_ctrl = true;   // compute shader switcher — on by default

    // ── Frame time rolling buffers ────────────────────────────────────────
    std::vector<float> frameTimes = std::vector<float>(128, 0.0f);
    std::vector<float> fpsHistory = std::vector<float>(128, 0.0f);
    float minFrameTime = 0.0f;
    float maxFrameTime = 0.0f;
    float avgFrameTime = 0.0f;

    // ── Log console ───────────────────────────────────────────────────────
    std::vector<std::string> logMessages;
    bool           autoScrollLog = true;
    ImGuiTextFilter logFilter;

    // ── Style ─────────────────────────────────────────────────────────────
    bool  darkMode = true;
    float uiScale = 1.0f;
};

extern DebugUIState g_debugUI;

// ── Public API ────────────────────────────────────────────────────────────────
void debug_ui_init(Engine* e);
void debug_ui_shutdown();
void debug_ui_update(float deltaTime);
void debug_ui_log(const char* fmt, ...);
void debug_ui_log(const std::string& msg);

// Main render — engine pointer gives access to live stats and scene data
void debug_ui_render(Engine* e);

// Individual panels
void debug_ui_render_performance_window();
void debug_ui_render_scene_debug_window(Engine* e);
void debug_ui_render_renderer_stats_window(Engine* e);
void debug_ui_render_memory_stats_window(Engine* e);
void debug_ui_render_log_console_window();
void debug_ui_render_input_debug_window();
void debug_ui_render_style_editor();
void debug_ui_render_imgui_demo();
void debug_ui_render_background_ctrl(Engine* e);  // compute shader switcher