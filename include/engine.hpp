#pragma once

#include <vulkan/vulkan_core.h>
#include <GLFW/glfw3.h>
#include <stdbool.h>

typedef struct Engine {
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    VkSurfaceKHR surface;
    GLFWwindow* window;
    VkDebugUtilsMessengerEXT debug_messenger;
} Engine;

// Function declarations
void init(Engine* engine);                  // Fixed: added Engine* parameter
void init_vulkan(Engine* engine);
void init_swapchain(Engine* engine);
void init_commands(Engine* engine);
void init_sync_structures(Engine* engine);
void engine_cleanup(Engine* engine);
void engine_draw_frame(Engine* engine);

