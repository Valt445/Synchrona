#pragma once

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
#include <string>

#include <GLFW/glfw3.h>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <loader.h>
#include "camer.h"
#include "debug_ui.h"
#include "descriptors.h"
#include "images.h"
#include "helper.h"
#include "graphics_pipeline.h"
#include "ibl.h"

// ─── Vulkan error check ───────────────────────────────────────────────────────
#define VK_CHECK(x)                                                         \
    do {                                                                    \
        VkResult err = x;                                                   \
        if (err != VK_SUCCESS) {                                            \
            std::cerr << "Vulkan error in " << #x << ": " << err << "\n";  \
            std::abort();                                                   \
        }                                                                   \
    } while (0)

// ─── DeletionQueue ────────────────────────────────────────────────────────────
struct DeletionQueue {
    std::deque<std::function<void()>> deletors;
    void push_function(std::function<void()>&& fn) { deletors.push_back(fn); }
    void flush() {
        for (auto it = deletors.rbegin(); it != deletors.rend(); ++it) (*it)();
        deletors.clear();
    }
};

// CameraData and MeshPushConstants are defined in types.h — do NOT redeclare here.

// ─── Compute background push constants ───────────────────────────────────────
struct ScenePushConstants {
    glm::vec4 data1;
    glm::vec4 data2;
    glm::vec4 data3;
    glm::vec4 data4;
};

// ─── Compute effect ───────────────────────────────────────────────────────────
struct ComputeEffect {
    const char* name;
    VkPipeline         pipeline;
    VkPipelineLayout   layout;
    ScenePushConstants effectData;
};

// ─── Memory stats ─────────────────────────────────────────────────────────────
struct MemoryStats {
    size_t totalMemoryBytes = 0;
    size_t imageMemoryBytes = 0;
    size_t bufferMemoryBytes = 0;
    size_t swapchainMemoryBytes = 0;
};

// ─── Per-frame GPU resources ──────────────────────────────────────────────────
struct FrameData {
    VkCommandPool    commandPool;
    VkCommandBuffer  mainCommandBuffer;
    VkSemaphore      swapchainSemaphore;
    VkSemaphore      renderSemaphore;
    VkFence          renderFence;
    DeletionQueue    deletionQueue;
    DescriptorAllocator frameDescriptors;

    // Per-frame camera UBO — one per frame so triple-buffering doesn't race
    AllocatedBuffer  cameraBuffer{};
};

constexpr unsigned int FRAME_OVERLAP = 3;

// ─── Engine ───────────────────────────────────────────────────────────────────
struct Engine {
    int width = 2560;
    int height = 1440;
    int   frameNumber = 0;
    float deltaTime = 0.0f;  // seconds since last frame

    VkInstance               instance = VK_NULL_HANDLE;
    VkPhysicalDevice         physicalDevice = VK_NULL_HANDLE;
    VkDevice                 device = VK_NULL_HANDLE;
    VkQueue                  graphicsQueue = VK_NULL_HANDLE;
    VkSurfaceKHR             surface = VK_NULL_HANDLE;
    GLFWwindow* window = nullptr;
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;

    VkSwapchainKHR             swapchain = VK_NULL_HANDLE;
    std::vector<VkImage>       swapchainImages;
    std::vector<VkImageView>   swapchainImageViews;
    VkFormat                   swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D                 swapchainExtent{};
    uint32_t                   graphicsQueueFamily = 0;

    DeletionQueue mainDeletionQueue;
    FrameData     frames[FRAME_OVERLAP];
    VmaAllocator  allocator = VK_NULL_HANDLE;

    std::vector<AllocatedImage> sceneTextures;
    uint32_t nextBindlessTextureIndex = 13;

    AllocatedImage drawImage{};
    VkExtent2D     drawExtent{};

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;

    DescriptorAllocator      globalDescriptorAllocator;
    VkDescriptorSet          drawImageDescriptors = VK_NULL_HANDLE;
    VkDescriptorSetLayout    drawImageDescriptorLayout = VK_NULL_HANDLE;

    VkPipeline       gradientPipeline = VK_NULL_HANDLE;
    VkPipelineLayout gradientPipelineLayout = VK_NULL_HANDLE;

    VkFence          immFence = VK_NULL_HANDLE;
    VkCommandBuffer  immCommandBuffer = VK_NULL_HANDLE;
    VkCommandPool    immCommandPool = VK_NULL_HANDLE;
    VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;

    std::vector<ComputeEffect> backgroundEffects;
    int currentBackgroundEffect = 0;

    MemoryStats memoryStats{};
    Utils       util;

    bool displayShadowMap = false;
    bool filterPCF = true;
    float zNear = 1.0f;
    float zFar = 96.0f;
    std::vector<std::string> sceneNames;
    int32_t sceneIndex = 0;

    AllocatedImage   shadowMapImage{};
    VkSampler        shadowMapSampler = VK_NULL_HANDLE;
    VkPipeline       shadowPipeline = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout = VK_NULL_HANDLE;
    uint32_t       shadowMapBindlessIndex = 5;  // slots 0-4 reserved
    glm::mat4        lightViewProj = glm::mat4(1.0f);  // updated every frame


    PipelineBuilder  pipelineBuilder;
    VkPipelineLayout trianglePipelineLayout = VK_NULL_HANDLE;
    VkPipeline       trianglePipeline = VK_NULL_HANDLE;
    VkPipelineLayout meshPipelineLayout = VK_NULL_HANDLE;
    VkPipeline       meshPipeline = VK_NULL_HANDLE;

    GPUMeshBuffers   rectangle{};
    AllocatedBuffer  vertexBuffer{};
    AllocatedBuffer  indexBuffer{};

    AllocatedImage whiteImage{};
    AllocatedImage blackImage{};
    AllocatedImage greyImage{};
    AllocatedImage errrorImage{};
    AllocatedImage depthImage{};

    VkSampler defaultSamplerLinear = VK_NULL_HANDLE;
    VkSampler defaultSamplerNearest = VK_NULL_HANDLE;

    VkDescriptorSetLayout singleImageDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet       singleImageDescriptorSet = VK_NULL_HANDLE;
    
    std::filesystem::path sceneBasePath;

    uint32_t cubeIndexCount = 0;

    DescriptorAllocator frameDescriptors;

    VkDescriptorPool      bindlessPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout bindlessLayout = VK_NULL_HANDLE;
    VkDescriptorSet       bindlessSet = VK_NULL_HANDLE;
    glm::vec3 sunDirection = glm::normalize(glm::vec3(0.3f, 0.6f, 0.4f));
    float     sunIntensity = 3.0f;

    uint32_t nextTextureIndex = 0;
    VkDescriptorSet meshTextureSet = VK_NULL_HANDLE;

    std::vector<std::shared_ptr<MeshAsset>> testMeshes;
    bool resize_requested = false;

    uint32_t textureCount = 0;
    std::unordered_map<std::string, uint32_t> textureNameIndexMap;

    Camera mainCamera;
    bool   keys[1024]{};

    //IBL
    AllocatedImage hdrImage{};
    AllocatedImage envCubemap{};
    AllocatedImage irradianceMap{};   // ← was irradiancemap, fix the typo
    AllocatedImage prefilterMap{};
    AllocatedImage brdfLUT{};

    // equirect → cubemap
    VkPipeline           equirectPipeline = VK_NULL_HANDLE;
    VkPipelineLayout     equirectLayout = VK_NULL_HANDLE;
    VkDescriptorSet      equirectSet = VK_NULL_HANDLE;
    VkDescriptorSetLayout equirectSetLayout = VK_NULL_HANDLE;

    // irradiance
    VkPipeline           irradiancePipeline = VK_NULL_HANDLE;
    VkPipelineLayout     irradianceLayout = VK_NULL_HANDLE;
    VkDescriptorSet      irradianceSet = VK_NULL_HANDLE;
    VkDescriptorSetLayout irradianceSetLayout = VK_NULL_HANDLE;

    // prefilter — one descriptor set per mip level
    VkPipeline            prefilterPipeline = VK_NULL_HANDLE;
    VkPipelineLayout      prefilterLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout prefilterSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet       prefilterSets[5] = {};
    VkImageView           prefilterMipViews[5] = {}; // one view per mip

    // brdf lut
    VkPipeline            brdfLutPipeline = VK_NULL_HANDLE;
    VkPipelineLayout      brdfLutLayout = VK_NULL_HANDLE;
    VkDescriptorSet       brdfLutSet = VK_NULL_HANDLE;
    VkDescriptorSetLayout brdfLutSetLayout = VK_NULL_HANDLE;

    // bindless slots for IBL textures
    uint32_t iblIrradianceIndex = 0;
    uint32_t iblPrefilterIndex = 1;
    uint32_t iblBrdfLutIndex = 12;


    //skybox 
    VkPipeline       skyboxPipeline = VK_NULL_HANDLE;
    VkPipelineLayout skyboxPipelineLayout = VK_NULL_HANDLE;
    float            skyTime = 0.0f;
    float            cloudCoverage = 0.6f;
    float            cloudSpeed = 1.0f;

    AllocatedImage msaaImage{};
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_4_BIT;

    // ── Per-frame draw stats — written by draw_geometry, read by debug UI
    uint32_t lastDrawCalls = 0;
    uint32_t lastTriangles = 0;

    uint32_t mipLevels = 1;
};

extern Engine* engine;

// ─── Function declarations ────────────────────────────────────────────────────

// Init
void init(Engine* e, uint32_t width, uint32_t height);
void init_vulkan(Engine* e);
void init_swapchain(Engine* e, uint32_t width, uint32_t height);
void init_descriptors(Engine* e);
void init_samplers(Engine* e);
void init_commands(Engine* e);
void init_camera_buffers(Engine* e);
void init_sync_structures(Engine* e);
void init_pipelines(Engine* e);
void init_background_pipelines(Engine* e);
void init_mesh_pipelines(Engine* e);
void init_imgui(Engine* e);
void init_default_data(Engine* e);
void init_debug_ui(Engine* e);
void init_depth_image(Engine* e, uint32_t width, uint32_t height);

// Frame
FrameData& get_current_frame(Engine* e);
void engine_draw_frame(Engine* e);
void update_uniform_buffers(Engine* e);

// Draw
void draw_geometry(Engine* e, VkCommandBuffer cmd);
void draw_background(VkCommandBuffer cmd, Engine* e);
void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView, Engine* e);

// Swapchain
void create_swapchain(Engine* e, uint32_t width, uint32_t height);
void destroy_swapchain(Engine* e);
void resize_swapchain(Engine* e);

// Draw image
void create_draw_image(Engine* e, uint32_t width, uint32_t height);
void destroy_draw_image(Engine* e);
void destroy_depth_image(Engine* e);

// Cleanup
void engine_cleanup(Engine* e);

// Buffers
AllocatedBuffer create_buffer(VmaAllocator allocator, size_t allocSize,
    VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
void destroy_buffer(const AllocatedBuffer& buffer, Engine* e);
// destroy_buffer(VmaAllocator) is defined in vulkan_core.cpp
GPUMeshBuffers uploadMesh(Engine* e, std::span<uint32_t> indices, std::span<Vertex> vertices);

// Images
AllocatedImage create_image(Engine* e, VkExtent3D size, VkFormat format,
    VkImageUsageFlags usage, bool mipmapped = false);
AllocatedImage create_image(void* data, Engine* e, VkExtent3D size, VkFormat format,
    VkImageUsageFlags usage, bool mipmapped = false);
void destroy_image(const AllocatedImage& image, Engine* e);
void upload_texture_to_bindless(Engine* e, AllocatedImage img, VkSampler sampler, uint32_t index);
AllocatedImage create_msaa_image(Engine* e, VkExtent3D size, VkFormat format,
    VkImageUsageFlags usage);
// Helpers
VkCommandBufferBeginInfo command_buffer_info(VkCommandBufferUsageFlags flags);
VkFenceCreateInfo        fence_create_info(VkFenceCreateFlags flags);
VkSemaphoreCreateInfo    semaphore_create_info(VkSemaphoreCreateFlags flags);
VkSemaphoreSubmitInfo    semaphore_submit_info(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore);
VkCommandBufferSubmitInfo command_buffer_submit_info(VkCommandBuffer cmd);
VkSubmitInfo2 submit_info(VkCommandBufferSubmitInfo* cmd,
    VkSemaphoreSubmitInfo* signalSemaphoreInfo,
    VkSemaphoreSubmitInfo* waitSemaphoreInfo);
VkRenderingAttachmentInfo attachment_info(VkImageView view, VkClearValue* clear, VkImageLayout layout);
void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function, Engine* e);

void calculateTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

//shadow
void  init_shadow_map(Engine* e, uint32_t width, uint32_t height);
void init_shadow_pipeline(Engine* e);
void draw_shadow_pass(Engine* e, VkCommandBuffer cmd);
void draw_skybox(Engine* e, VkCommandBuffer cmd);

// Misc
VkFormat find_depth_format(VkPhysicalDevice physicalDevice);
void init_triangle_pipeline(Engine* e);
void CreateShaderModule(const char* filePath, Engine* e, VkShaderModule shaderName);