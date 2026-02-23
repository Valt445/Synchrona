#pragma once


#include <imgui.h>

// Debug UI state
struct Engine;
struct DebugUIState;

// Global debug UI state
extern DebugUIState g_debugUI;

// Initialization
void debug_ui_init(Engine* engine);
void debug_ui_shutdown();
void debug_ui_update(float deltaTime);
void debug_ui_render();

// Individual debug windows
void debug_ui_render_performance_window();
void debug_ui_render_scene_debug_window();
void debug_ui_render_renderer_stats_window();
void debug_ui_render_memory_stats_window();
void debug_ui_shutdown();
void debug_ui_render_objects_window();
void debug_ui_render_lights_window();
void switch_shader();

// Helper functions
void debug_ui_update_frame_stats(float deltaTime);
