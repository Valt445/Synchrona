#pragma once

#include <cstdint>
#include <vulkan/vulkan_core.h>
#include <GLFW/glfw3.h>
#include <stdbool.h>
#include <vector>
#include <iostream>

#define VK_CHECK(x)                                                     \
    do {                                                                \
        VkResult err = x;                                               \
        if (err != VK_SUCCESS) {                                        \
            std::cerr << "Detected Vulkan error: " << err << std::endl; \
            std::abort();                                               \
                                                                        \
        }                                                               \
    } while (0)                                                         \


typedef struct FrameData
{
    VkCommandPool commandPool;
    VkCommandBuffer mainCommandBuffer;
    
} FrameData;

constexpr unsigned int FRAME_OVERLAP = 2;

typedef struct Engine {
    int frameNumber;
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    VkSurfaceKHR surface;
    GLFWwindow* window;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    VkFormat swapchainImageFormat;
    VkExtent2D swapchainExtent;
    uint32_t graphicsQueueFamily;
    FrameData frames[FRAME_OVERLAP];
} Engine;


// Function declarations
void init(Engine* engine, uint32_t width, uint32_t height);
FrameData& get_current_frame();
void init_vulkan(Engine* engine);
void init_swapchain(Engine* engine, uint32_t width, uint32_t height);
void init_commands(Engine* engine);
void init_sync_structures(Engine* engine);
void engine_cleanup(Engine* engine);
void engine_draw_frame(Engine* engine);



        
