#pragma once

// Basic project/types header (your project should define AllocatedBuffer, GPUMeshBuffers, Vertex, etc.)
#include <cstdint>
#include <types.h>

#include <vulkan/vulkan_core.h>
#include <vma/vk_mem_alloc.h>

#include <deque>
#include <functional>
#include <vector>
#include <span>
#include <cstddef>
#include <iostream>

#include <GLFW/glfw3.h>
#include <glm/vec4.hpp>
#include <loader.h>

// Vulkan error checking macro

#define VK_CHECK(x)                                                     \
    do {                                                                \
        VkResult err = x;                                               \
        if (err != VK_SUCCESS) {                                        \
            std::cerr << "Vulkan error in " << #x << ": " << err << std::endl; \
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

struct DebugUIState;
struct PipelineBuilder;

struct PushConstants
{
    glm::vec4 data1;
    glm::vec4 data2;
    glm::vec4 data3;
    glm::vec4 data4;
};

struct MemoryStats
{
    size_t totalMemoryBytes;
    size_t imageMemoryBytes;
    size_t bufferMemoryBytes;
    size_t swapchainMemoryBytes;
}; 

struct ComputeEffect
{
    const char* name;

    VkPipeline pipeline;
    VkPipelineLayout layout;

    PushConstants data;
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

    int width = 1720;
    int height = 1200;
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

    VkFence immFence;
    VkCommandBuffer immCommandBuffer;
    VkCommandPool immCommandPool;
    VkDescriptorPool imguiDescriptorPool;

    std::vector<ComputeEffect> backgroundEffects;
    int currentBackgroundEffect{0};
    MemoryStats memoryStats;
		
    Utils util;

    // Graphics Pipeline
    PipelineBuilder pipelineBuilder;
    VkPipelineLayout trianglePipelineLayout;
    VkPipeline trianglePipeline;
    
    VkPipelineLayout meshPipelineLayout;
    VkPipeline meshPipeline;

    GPUMeshBuffers rectangle;

    AllocatedBuffer vertexBuffer;
    AllocatedBuffer indexBuffer;

    std::vector<std::shared_ptr<MeshAsset>> testMeshes;
    bool resize_requested;

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

void create_swapchain(Engine* e, uint32_t width, uint32_t height);
void destroy_swapchain(Engine* e);

VkSemaphoreSubmitInfo semaphore_submit_info(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore);
VkCommandBufferSubmitInfo command_buffer_submit_info(VkCommandBuffer cmd);
VkSubmitInfo2 submit_info(VkCommandBufferSubmitInfo* cmd,
                          VkSemaphoreSubmitInfo* signalSemaphoreInfo,
                          VkSemaphoreSubmitInfo* waitSemaphoreInfo);

void init_descriptors(Engine* engine);
void init_pipelines(Engine* engine);
void init_background_pipelines(Engine* engine);
void init_mesh_pipelines(Engine* e);

void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function, Engine* e);
void init_imgui(Engine* e);
void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView, Engine* e);
void CreateShaderModule(const char* filePath, Engine* e ,VkShaderModule shaderName);

// Dynamic Rendering!!
VkRenderingAttachmentInfo attachment_info(VkImageView view, VkClearValue* clear, VkImageLayout layout);

// Graphics Pipeline
void init_triangle_pipeline(Engine* e);
void draw_geometry(Engine* e, VkCommandBuffer cmd);

// Buffer Allocation (signatures updated to match implementations that use VmaAllocator)
AllocatedBuffer create_buffer(VmaAllocator allocator, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
void destroy_buffer(const AllocatedBuffer& buffer, VmaAllocator allocator);

GPUMeshBuffers uploadMesh(Engine* e, std::span<uint32_t> indices, std::span<Vertex> vertices);

void resize_swapchain(Engine* e);
void init_default_data(Engine* e);
void create_draw_image(Engine* e, uint32_t width, uint32_t height);
void destroy_draw_image(Engine* e);

