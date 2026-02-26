#pragma once

// ============================================================================
// INCLUDES - EXACT SAME AS ORIGINAL (Keep dependencies identical)
// ============================================================================

#include <cstdint>
#include <types.h>

#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>

#include <deque>
#include <functional>
#include <vector>
#include <span>
#include <cstddef>
#include <iostream>
#include <unordered_map>

#include <GLFW/glfw3.h>
#include <glm/vec4.hpp>
#include <loader.h>
#include "camer.h"

// ============================================================================
// VULKAN ERROR CHECKING MACRO
// ============================================================================

#define VK_CHECK(x)                                                     \
    do {                                                                \
        VkResult err = x;                                               \
        if (err != VK_SUCCESS) {                                        \
            std::cerr << "Vulkan error in " << #x << ": " << err << std::endl; \
            std::abort();                                               \
        }                                                               \
    } while (0)

// ============================================================================
// UTILITY STRUCTURES
// ============================================================================

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

// Forward declarations
struct DebugUIState;
struct PipelineBuilder;

// ============================================================================
// PUSH CONSTANTS AND COMPUTE STRUCTURES
// ============================================================================

struct ScenePushConstants
{
    glm::vec4 data1;
    glm::vec4 data2;
    glm::vec4 data3;
    glm::vec4 data4;
};

struct ComputeEffect
{
    const char* name;
    VkPipeline pipeline;
    VkPipelineLayout layout;
    ScenePushConstants effectData;
};

struct ComputePushConstants {
    glm::mat4 worldMatrix;
    VkDeviceAddress vertexBuffer;
    uint32_t textureIndex;
    uint32_t padding;
};

// ============================================================================
// MEMORY STATISTICS
// ============================================================================

struct MemoryStats
{
    size_t totalMemoryBytes;
    size_t imageMemoryBytes;
    size_t bufferMemoryBytes;
    size_t swapchainMemoryBytes;
};

// ============================================================================
// PER-FRAME DATA (synchronization and resources for each frame)
// ============================================================================

struct FrameData {
    VkCommandPool commandPool;
    VkCommandBuffer mainCommandBuffer;
    VkSemaphore swapchainSemaphore;
    VkSemaphore renderSemaphore;
    VkFence renderFence;
    DeletionQueue deletionQueue;
    DescriptorAllocator frameDescriptors;
};

constexpr unsigned int FRAME_OVERLAP = 3;

// ============================================================================
// CORE ENGINE STRUCTURE (organized by subsystem)
// ============================================================================

extern Engine* engine;  // Declaration

struct Engine {

    // === VULKAN CORE (from vulkan_core.cpp) ===
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    uint32_t graphicsQueueFamily;
    VkDebugUtilsMessengerEXT debug_messenger;
    VmaAllocator allocator;

    // === WINDOW & SURFACE ===
    GLFWwindow* window;
    VkSurfaceKHR surface;
    int width = 1720;
    int height = 1200;

    // === SWAPCHAIN (from swapchain.cpp) ===
    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    VkFormat swapchainImageFormat;
    VkExtent2D swapchainExtent;
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;

    // === FRAME MANAGEMENT (from commands_and_sync.cpp) ===
    FrameData frames[FRAME_OVERLAP];
    int frameNumber = 0;
    bool resize_requested;
    DeletionQueue mainDeletionQueue;

    // === RENDERING TARGETS (from memory.cpp) ===
    AllocatedImage drawImage;
    VkExtent2D drawExtent;
    AllocatedImage depthImage;

    // === DESCRIPTORS (from descriptors.cpp) ===
    DescriptorAllocator globalDescriptorAllocator;
    DescriptorAllocator frameDescriptors;   // per frame descriptor allocator
    VkDescriptorSetLayout drawImageDescriptorLayout;
    VkDescriptorSet drawImageDescriptors;
    VkDescriptorPool bindlessPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout bindlessLayout = VK_NULL_HANDLE;
    VkDescriptorSet bindlessSet = VK_NULL_HANDLE;
    VkDescriptorSetLayout singleImageDescriptorSetLayout;
    VkDescriptorSet singleImageDescriptorSet;
    VkDescriptorSet meshTextureSet = VK_NULL_HANDLE;

    // === GRAPHICS PIPELINES (from pipelines.cpp) ===
    PipelineBuilder pipelineBuilder;
    VkPipelineLayout gradientPipelineLayout;
    VkPipeline gradientPipeline;
    VkPipelineLayout trianglePipelineLayout;
    VkPipeline trianglePipeline;
    VkPipelineLayout meshPipelineLayout;
    VkPipeline meshPipeline;
    std::vector<ComputeEffect> backgroundEffects;
    int currentBackgroundEffect{ 0 };

    // === MESHES (from mesh.cpp) ===
    GPUMeshBuffers rectangle;
    AllocatedBuffer vertexBuffer;
    AllocatedBuffer indexBuffer;
    uint32_t cubeIndexCount;
    std::vector<std::shared_ptr<MeshAsset>> testMeshes;

    // === TEXTURES & BINDLESS (from textures.cpp) ===
    std::vector<AllocatedImage> sceneTextures;
    uint32_t nextBindlessTextureIndex = 0;
    uint32_t nextTextureIndex = 0;
    uint32_t textureCount{ 0 };
    std::unordered_map<std::string, uint32_t> textureNameIndexMap;

    // === DEFAULT TEXTURES (from init_default_data in engine.cpp) ===
    AllocatedImage whiteImage;
    AllocatedImage blackImage;
    AllocatedImage greyImage;
    AllocatedImage errrorImage;

    // === SAMPLERS ===
    VkSampler defaultSamplerLinear;
    VkSampler defaultSamplerNearest;

    // === IMMEDIATE SUBMIT (from immediate_submit.cpp) ===
    VkFence immFence;
    VkCommandBuffer immCommandBuffer;
    VkCommandPool immCommandPool;

    // === IMGUI (from imgui_integration.cpp) ===
    VkDescriptorPool imguiDescriptorPool;

    // === UTILITIES ===
    Utils util;
    MemoryStats memoryStats;
    Camera mainCamera;
    bool keys[1024];
};

// ============================================================================
// GLOBAL ENGINE POINTER
// ============================================================================

extern Engine* engine;

// ============================================================================
// INITIALIZATION & CLEANUP (engine.cpp)
// ============================================================================

void init(Engine* engine, uint32_t width, uint32_t height);
void engine_cleanup(Engine* engine);
void engine_draw_frame(Engine* engine);
void init_default_data(Engine* e);
void create_draw_image(Engine* e, uint32_t width, uint32_t height);
void destroy_draw_image(Engine* e);

// ============================================================================
// VULKAN CORE INITIALIZATION (vulkan_core.cpp)
// ============================================================================

void init_vulkan(Engine* engine);

// ============================================================================
// SWAPCHAIN MANAGEMENT (swapchain.cpp)
// ============================================================================

void init_swapchain(Engine* engine, uint32_t width, uint32_t height);
void create_swapchain(Engine* e, uint32_t width, uint32_t height);
void destroy_swapchain(Engine* e);
void resize_swapchain(Engine* e);

// ============================================================================
// COMMANDS & SYNCHRONIZATION (commands_and_sync.cpp)
// ============================================================================

void init_commands(Engine* engine);
void init_sync_structures(Engine* engine);
FrameData& get_current_frame(Engine* engine);

VkCommandBufferBeginInfo command_buffer_info(VkCommandBufferUsageFlags flags);
VkFenceCreateInfo fence_create_info(VkFenceCreateFlags flags);
VkSemaphoreCreateInfo semaphore_create_info(VkSemaphoreCreateFlags flags);
VkSemaphoreSubmitInfo semaphore_submit_info(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore);
VkCommandBufferSubmitInfo command_buffer_submit_info(VkCommandBuffer cmd);
VkSubmitInfo2 submit_info(VkCommandBufferSubmitInfo* cmd,
    VkSemaphoreSubmitInfo* signalSemaphoreInfo,
    VkSemaphoreSubmitInfo* waitSemaphoreInfo);

// ============================================================================
// MEMORY & BUFFER/IMAGE MANAGEMENT (memory.cpp)
// ============================================================================

AllocatedBuffer create_buffer(VmaAllocator allocator, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
void destroy_buffer(const AllocatedBuffer& buffer, VmaAllocator allocator);

AllocatedImage create_image(Engine* e, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
AllocatedImage create_image(void* data, Engine* e, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
void destroy_image(const AllocatedImage& image, Engine* e);

VkRenderingAttachmentInfo attachment_info(VkImageView view, VkClearValue* clear, VkImageLayout layout);

// ============================================================================
// DESCRIPTORS (descriptors.cpp)
// ============================================================================

void init_descriptors(Engine* engine);

// ============================================================================
// PIPELINES (pipelines.cpp)
// ============================================================================

void init_pipelines(Engine* engine);
void init_background_pipelines(Engine* engine);
void init_mesh_pipelines(Engine* e);


// ============================================================================
// MESH MANAGEMENT (mesh.cpp)
// ============================================================================

GPUMeshBuffers uploadMesh(Engine* e, std::span<uint32_t> indices, std::span<Vertex> vertices);

// ============================================================================
// TEXTURES & BINDLESS (textures.cpp)
// ============================================================================

void upload_texture_to_bindless(Engine* e, AllocatedImage img, VkSampler sampler, uint32_t index);

// ============================================================================
// RENDERING (rendering.cpp)
// ============================================================================

void draw_background(VkCommandBuffer cmd, Engine* engine);
void draw_geometry(Engine* e, VkCommandBuffer cmd);
void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView, Engine* e);

// ============================================================================
// IMMEDIATE COMMAND SUBMISSION (immediate_submit.cpp)
// ============================================================================

void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function, Engine* e);

// ============================================================================
// IMGUI INTEGRATION (imgui_integration.cpp)
// ============================================================================

void init_imgui(Engine* e);