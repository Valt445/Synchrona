#include "engine.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "VkBootstrap.h"

// Initialize the Engine struct to zero / defaults
void init(Engine* engine) {
    memset(engine, 0, sizeof(*engine));
    init_vulkan(engine);
    init_swapchain(engine);
    init_commands(engine);
    init_sync_structures(engine);
}

// Create Vulkan instance and debug messenger
void init_vulkan(Engine* engine) {
    vkb::InstanceBuilder builder;

    auto inst_ret = builder.set_app_name("Malike")
                           .request_validation_layers(true)
                           .use_default_debug_messenger()
                           .require_api_version(1, 3, 0)
                           .build();

    if (!inst_ret) {
        printf("Failed to create Vulkan instance!\n");
        exit(1);
    }

    vkb::Instance vkb_inst = inst_ret.value();
    engine->instance = vkb_inst.instance;
    engine->debug_messenger = vkb_inst.debug_messenger;

    // Initialize GLFW window
    if (!glfwInit()) {
        printf("Failed to initialize GLFW\n");
        exit(1);
    }

    if (!glfwVulkanSupported()) {
        printf("GLFW reports Vulkan not supported!\n");
        exit(1);
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    engine->window = glfwCreateWindow(800, 600, "Vulkan Window", nullptr, nullptr);
    if (!engine->window) {
        printf("Failed to create GLFW window\n");
        exit(1);
    }

    // Create Vulkan surface
    if (glfwCreateWindowSurface(engine->instance, engine->window, nullptr, &engine->surface) != VK_SUCCESS) {
        printf("Failed to create Vulkan surface\n");
        exit(1);
    }
}

// Placeholder: swapchain initialization
void init_swapchain(Engine* engine) {
    // TODO: Implement swapchain creation
    printf("Swapchain initialized (placeholder)\n");
}

// Placeholder: command buffers and pools
void init_commands(Engine* engine) {
    // TODO: Implement command pool / buffer creation
    printf("Commands initialized (placeholder)\n");
}

// Placeholder: semaphores, fences
void init_sync_structures(Engine* engine) {
    // TODO: Implement sync primitives
    printf("Sync structures initialized (placeholder)\n");
}

// Render a single frame (placeholder)
void engine_draw_frame(Engine* engine) {
    // TODO: Record command buffers and submit
    printf("Frame drawn (placeholder)\n");
}

// Cleanup Vulkan and GLFW resources
void engine_cleanup(Engine* engine) {
    if (engine->surface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(engine->instance, engine->surface, nullptr);
    if (engine->instance != VK_NULL_HANDLE)
        vkDestroyInstance(engine->instance, nullptr);
    if (engine->window)
        glfwDestroyWindow(engine->window);
    glfwTerminate();

    printf("Engine cleaned up.\n");
}

