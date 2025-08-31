#pragma once

#include <cstdint>
#include <vulkan/vulkan_core.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <iostream>
#include <deque>
#include <bits/stdc++.h>
#include <vk_mem_alloc.h>
#include "images.h"
#include "descriptors.h"
#include "helper.h"
#include "glm/glm.hpp"
// Vulkan error checking macro
#define VK_CHECK(x)                                                     \
    do {                                                                \
        VkResult err = x;                                               \
        if (err != VK_SUCCESS) {                                        \
            std::cerr << "Detected Vulkan error: " << err << std::endl; \
            std::abort();                                               \
        }                                                               \
    } while (0)


struct DeletionQueue
{
	std::deque<std::function<void()>> deletors;

	void push_function(std::function<void()>&& function) {
		deletors.push_back(function);
	}

	void flush() {
		// reverse iterate the deletion queue to execute all the functions
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
			(*it)(); //call functors
		}

		deletors.clear();
	}
};

struct PushConstants
{
	float time;
	glm::vec2 resolution;
	float pulse;
};

struct FrameData {
    VkCommandPool commandPool;
    VkCommandBuffer mainCommandBuffer;
    VkSemaphore swapchainSemaphore;
    VkSemaphore renderSemaphore;
    VkFence renderFence;
    DeletionQueue deletionQueue;
};
      
constexpr unsigned int FRAME_OVERLAP = 3;

// Core engine state
struct Engine {
    int frameNumber = 0;
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
    DeletionQueue mainDeletionQueue;
    FrameData frames[FRAME_OVERLAP];
    VmaAllocator allocator;
 
    AllocatedImage drawImage;
    VkExtent2D drawExtent;

		std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores; 
    
    DescriptorAllocator globalDescriptorAllocator;
    VkDescriptorSet drawImageDescriptors;
    VkDescriptorSetLayout drawImageDescriptorLayout;

    VkPipeline gradientPipeline;
    VkPipelineLayout gradientPipelineLayout; 

    Utils util;
};

// Global engine pointer (defined in .cpp)
extern Engine* engine;

// Function declarations
VkCommandBufferBeginInfo command_buffer_info(VkCommandBufferUsageFlags flags);
VkFenceCreateInfo fence_create_info(VkFenceCreateFlags flags);
VkSemaphoreCreateInfo semaphore_create_info(VkSemaphoreCreateFlags flags);
void draw_background(VkCommandBuffer cmd, Engine* engine);

void init(Engine* engine, uint32_t width, uint32_t height);
FrameData& get_current_frame(Engine* engine);

void init_vulkan(Engine* engine);
void init_swapchain(Engine* engine, uint32_t width, uint32_t height);
void init_commands(Engine* engine);
void init_sync_structures(Engine* engine);
void engine_cleanup(Engine* engine);
void engine_draw_frame(Engine* engine);

VkSemaphoreSubmitInfo semaphore_submit_info(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore);
VkCommandBufferSubmitInfo command_buffer_submit_info(VkCommandBuffer cmd);
VkSubmitInfo2 submit_info(VkCommandBufferSubmitInfo* cmd,
                          VkSemaphoreSubmitInfo* signalSemaphoreInfo,
                          VkSemaphoreSubmitInfo* waitSemaphoreInfo);

void init_descriptors(Engine* engine);
void init_pipelines(Engine* engine);
void init_background_pipelines(Engine* engine);
