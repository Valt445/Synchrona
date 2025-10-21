#include "engine.hpp"
#include "VkBootstrap.h"
#include "graphics_pipeline.h"
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <cstdint>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <types.h>
#include <vulkan/vulkan_core.h>
#include <iostream>
#include "loader.h"

// Define the global engine pointer declared as `extern` in the header
Engine* engine = nullptr;

// ------------------------- Initialization -------------------------

// Initialize the Engine
void init(Engine* e, uint32_t x, uint32_t y) {
    *e = Engine{};
    ::engine = e;

    init_vulkan(e);
    e->swapchainImageFormat = VK_FORMAT_B8G8R8A8_SRGB;
    init_swapchain(e, x, y);
    init_commands(e);
    init_sync_structures(e);
    init_descriptors(e);
    init_pipelines(e);
    init_mesh_pipelines(e);
    init_default_data(e);
    init_imgui(e);
}

// Create Vulkan instance, surface, device and queue

void init_vulkan(Engine* e) {
    vkb::InstanceBuilder builder;

    auto inst_ret = builder.set_app_name("Malike")
                               .request_validation_layers(true)
                               .use_default_debug_messenger()
                               .require_api_version(1, 3, 0)
                               .build();

    if (!inst_ret) {
        std::printf("Failed to create Vulkan instance!\n");
        std::exit(1);
    }

    vkb::Instance vkb_inst = inst_ret.value();
    e->instance = vkb_inst.instance;
    e->debug_messenger = vkb_inst.debug_messenger;

    if (!glfwInit()) {
        std::printf("Failed to initialize GLFW\n");
        std::exit(1);
    }

    if (!glfwVulkanSupported()) {
        std::printf("GLFW reports Vulkan not supported!\n");
        std::exit(1);
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);  // FIXED: Enable drag-resize (Linux/GLFW fix)

    e->window = glfwCreateWindow(e->width, e->height, "Vulkan Window", nullptr, nullptr);
    if (!e->window) {
        std::printf("Failed to create GLFW window\n");
        std::exit(1);
    }

    glfwSetWindowUserPointer(e->window, e);

    // FIXED: Set callback properly (was misplaced debounce block)


    glfwSetFramebufferSizeCallback(e->window, [](GLFWwindow* window, int width, int height) {
    Engine* engine = static_cast<Engine*>(glfwGetWindowUserPointer(window));
    if (engine) {
        // Handle minimization (width/height = 0)
        if (width == 0 || height == 0) {
            std::printf("📦 Window minimized\n");
            engine->resize_requested = true;
            return;
        }
        
        // Simple debounce - only resize if significantly different
        static int lastW = 0, lastH = 0;
        if (std::abs(width - lastW) > 2 || std::abs(height - lastH) > 2) {
            lastW = width; 
            lastH = height;
            
            engine->swapchainExtent.width = width;
            engine->swapchainExtent.height = height;
            engine->resize_requested = true;
            std::printf("📏 Resize requested: %dx%d\n", width, height);
        }
    }
    });

    
    if (glfwCreateWindowSurface(e->instance, e->window, nullptr, &e->surface) != VK_SUCCESS) {
        std::printf("Failed to create Vulkan surface\n");
        std::exit(1);
    }

    VkPhysicalDeviceVulkan13Features features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = nullptr,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE
    };

    VkPhysicalDeviceVulkan12Features features12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = nullptr,
        .descriptorIndexing = VK_TRUE,
        .bufferDeviceAddress = VK_TRUE
    };

    vkb::PhysicalDeviceSelector selector{ vkb_inst };
    vkb::PhysicalDevice physicalDevice = selector
        .set_minimum_version(1, 3)
        .set_required_features_13(features)
        .set_required_features_12(features12)
        .set_surface(e->surface)
        .select()
        .value();

    vkb::DeviceBuilder deviceBuilder{ physicalDevice };
    vkb::Device vkbDevice = deviceBuilder.build().value();

    e->device = vkbDevice.device;
    e->physicalDevice = physicalDevice.physical_device;

    e->graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    e->graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
    
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = e->physicalDevice;
    allocatorInfo.device = e->device;
    allocatorInfo.instance = e->instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &e->allocator));

    e->mainDeletionQueue.push_function([=]() { vmaDestroyAllocator(e->allocator); });
}
void init_pipelines(Engine* e) {
    init_background_pipelines(e);
}

void init_imgui(Engine* e)
{
    VkDescriptorPoolSize pool_sizes[] = { 
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } 
    };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    VK_CHECK(vkCreateDescriptorPool(e->device, &pool_info, nullptr, &e->imguiDescriptorPool));

    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForVulkan(e->window, true);

    VkPipelineRenderingCreateInfo pipelineRenderingInfo{};
    pipelineRenderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    pipelineRenderingInfo.colorAttachmentCount = 1;
    pipelineRenderingInfo.pColorAttachmentFormats = &e->swapchainImageFormat;

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = e->instance;
    init_info.PhysicalDevice = e->physicalDevice;
    init_info.Device = e->device;
    init_info.Queue = e->graphicsQueue;
    init_info.DescriptorPool = e->imguiDescriptorPool;

    uint32_t scImageCount = static_cast<uint32_t>(e->swapchainImages.size());
    if (scImageCount == 0) scImageCount = 2;
    init_info.MinImageCount = scImageCount;
    init_info.ImageCount = scImageCount;
    init_info.UseDynamicRendering = VK_TRUE;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.PipelineRenderingCreateInfo = pipelineRenderingInfo;

    ImGui_ImplVulkan_Init(&init_info);

    debug_ui_init(e);
    e->mainDeletionQueue.push_function([=]() {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        vkDestroyDescriptorPool(e->device, e->imguiDescriptorPool, nullptr);
    });
}

AllocatedBuffer create_buffer(
    VmaAllocator allocator,
    size_t allocSize,
    VkBufferUsageFlags usage,
    VmaMemoryUsage memoryUsage)
{
    AllocatedBuffer newBuffer{};

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = allocSize;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo vmaAllocInfo{};
    vmaAllocInfo.usage = memoryUsage;

    std::cerr << "➡️ Creating buffer: size=" << allocSize
              << " usage=" << usage
              << " memoryUsage=" << memoryUsage << std::endl;

    VkResult result = vmaCreateBuffer(
        allocator,
        &bufferInfo,
        &vmaAllocInfo,
        &newBuffer.buffer,
        &newBuffer.allocation,
        &newBuffer.info
    );

    if (result != VK_SUCCESS || newBuffer.buffer == VK_NULL_HANDLE) {
        std::cerr << "❌ Buffer creation FAILED! VkResult=" << result
                  << " | size=" << allocSize
                  << " | usage=" << usage
                  << " | memoryUsage=" << memoryUsage
                  << std::endl;
        newBuffer.buffer = VK_NULL_HANDLE;
        newBuffer.allocation = VK_NULL_HANDLE;
    } else {
        std::cerr << "✅ Buffer created successfully: "
                  << "handle=" << newBuffer.buffer
                  << " size=" << allocSize << std::endl;
    }

    return newBuffer;
}

void destroy_buffer(const AllocatedBuffer& buffer, Engine* e)
{
    vmaDestroyBuffer(e->allocator, buffer.buffer, buffer.allocation);
}

GPUMeshBuffers uploadMesh(Engine* e, std::span<uint32_t> indices, std::span<Vertex> vertices) {
    GPUMeshBuffers newSurface{};

    // --- Vertex Buffer ---
    size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
    if (vertexBufferSize == 0) {
        std::cerr << "❌ Vertex buffer size is 0!" << std::endl;
        return newSurface;
    }
    {
        AllocatedBuffer stagingBuffer = create_buffer(
            e->allocator,
            vertexBufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_CPU_ONLY
        );

        if (stagingBuffer.buffer == VK_NULL_HANDLE) {
            std::cerr << "❌ Failed to create vertex staging buffer!" << std::endl;
            return newSurface;
        }

        void* data;
        vmaMapMemory(e->allocator, stagingBuffer.allocation, &data);
        memcpy(data, vertices.data(), vertexBufferSize);
        vmaUnmapMemory(e->allocator, stagingBuffer.allocation);

        newSurface.vertexBuffer = create_buffer(
            e->allocator,
            vertexBufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        if (newSurface.vertexBuffer.buffer == VK_NULL_HANDLE) {
            std::cerr << "❌ Failed to create vertex buffer!" << std::endl;
            vmaDestroyBuffer(e->allocator, stagingBuffer.buffer, stagingBuffer.allocation);
            return newSurface;
        }

        immediate_submit([&](VkCommandBuffer cmd) {
            VkBufferCopy copyRegion{};
            copyRegion.size = vertexBufferSize;
            vkCmdCopyBuffer(cmd, stagingBuffer.buffer, newSurface.vertexBuffer.buffer, 1, &copyRegion);
        }, e);

        vmaDestroyBuffer(e->allocator, stagingBuffer.buffer, stagingBuffer.allocation);

        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = newSurface.vertexBuffer.buffer;
        newSurface.vertexBufferAddress = vkGetBufferDeviceAddress(e->device, &addressInfo);
    }

    // --- Index Buffer ---
    size_t indexBufferSize = indices.size() * sizeof(uint32_t);
    if (indexBufferSize > 0) {
        AllocatedBuffer stagingBuffer = create_buffer(
            e->allocator,
            indexBufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_CPU_ONLY
        );

        if (stagingBuffer.buffer == VK_NULL_HANDLE) {
            std::cerr << "❌ Failed to create index staging buffer!" << std::endl;
            return newSurface;
        }

        void* data;
        vmaMapMemory(e->allocator, stagingBuffer.allocation, &data);
        memcpy(data, indices.data(), indexBufferSize);
        vmaUnmapMemory(e->allocator, stagingBuffer.allocation);

        newSurface.indexBuffer = create_buffer(
            e->allocator,
            indexBufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        if (newSurface.indexBuffer.buffer == VK_NULL_HANDLE) {
            std::cerr << "❌ Failed to create index buffer!" << std::endl;
            vmaDestroyBuffer(e->allocator, stagingBuffer.buffer, stagingBuffer.allocation);
            return newSurface;
        }

        immediate_submit([&](VkCommandBuffer cmd) {
            VkBufferCopy copyRegion{};
            copyRegion.size = indexBufferSize;
            vkCmdCopyBuffer(cmd, stagingBuffer.buffer, newSurface.indexBuffer.buffer, 1, &copyRegion);
        }, e);

        vmaDestroyBuffer(e->allocator, stagingBuffer.buffer, stagingBuffer.allocation);
    }

    return newSurface;
}

void destroy_draw_image(Engine* e) {
    if (e->drawImage.imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(e->device, e->drawImage.imageView, nullptr);
        e->drawImage.imageView = VK_NULL_HANDLE;
    }
    if (e->drawImage.image != VK_NULL_HANDLE) {
        vmaDestroyImage(e->allocator, e->drawImage.image, e->drawImage.allocation);
        e->drawImage.image = VK_NULL_HANDLE; 
        e->drawImage.allocation = VK_NULL_HANDLE;
    }
}

void create_draw_image(Engine* e, uint32_t width, uint32_t height) {
    std::printf("🎨 Creating draw image %ux%u...\n", width, height);
    
    // Destroy old draw image if it exists
    destroy_draw_image(e);
    
    VkExtent3D drawImageExtent = { width, height, 1 };
    e->drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    e->drawImage.imageExtent = drawImageExtent;
    e->drawExtent = { width, height };

    VkImageUsageFlags drawImageUsages{};
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkImageCreateInfo rimg_info = image_create_info(e->drawImage.imageFormat, drawImageUsages, drawImageExtent);

    VmaAllocationCreateInfo rimg_allocinfo = {};
    rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(vmaCreateImage(e->allocator, &rimg_info, &rimg_allocinfo, &e->drawImage.image, &e->drawImage.allocation, nullptr));

    // Update memory stats
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(e->device, e->drawImage.image, &memReq);
    e->memoryStats.imageMemoryBytes = memReq.size; // Replace, don't accumulate
    e->memoryStats.totalMemoryBytes = e->memoryStats.swapchainMemoryBytes + e->memoryStats.imageMemoryBytes + e->memoryStats.bufferMemoryBytes;

    VkImageViewCreateInfo rview_info = imageview_create_info(e->drawImage.imageFormat, e->drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
    VkComponentMapping mapping = { 
        .r = VK_COMPONENT_SWIZZLE_IDENTITY, 
        .g = VK_COMPONENT_SWIZZLE_IDENTITY, 
        .b = VK_COMPONENT_SWIZZLE_IDENTITY, 
        .a = VK_COMPONENT_SWIZZLE_IDENTITY 
    };
    rview_info.components = mapping;
    VK_CHECK(vkCreateImageView(e->device, &rview_info, nullptr, &e->drawImage.imageView));

    // Update descriptor sets for the new draw image
    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imgInfo.imageView = e->drawImage.imageView;

    VkWriteDescriptorSet drawImageWrite = {};
    drawImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    drawImageWrite.pNext = nullptr;
    drawImageWrite.dstBinding = 0;
    drawImageWrite.dstSet = e->drawImageDescriptors;
    drawImageWrite.descriptorCount = 1;
    drawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    drawImageWrite.pImageInfo = &imgInfo;

    vkUpdateDescriptorSets(e->device, 1, &drawImageWrite, 0, nullptr);
    
    std::printf("✅ Draw image created: %ux%u\n", width, height);
}
void destroy_swapchain(Engine* e) {
    if (e->swapchain == VK_NULL_HANDLE) return;
    
    std::printf("🧹 Destroying swapchain...\n");
    vkDeviceWaitIdle(e->device);

    // Destroy image views
    for (auto view : e->swapchainImageViews) {
        vkDestroyImageView(e->device, view, nullptr);
    }
    e->swapchainImageViews.clear();
    e->swapchainImages.clear();

    // Destroy semaphores
    for (size_t i = 0; i < e->imageAvailableSemaphores.size(); i++) {
        if (e->imageAvailableSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(e->device, e->imageAvailableSemaphores[i], nullptr);
        }
        if (e->renderFinishedSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(e->device, e->renderFinishedSemaphores[i], nullptr);
        }
    }
    e->imageAvailableSemaphores.clear();
    e->renderFinishedSemaphores.clear();

    // Destroy swapchain last
    vkDestroySwapchainKHR(e->device, e->swapchain, nullptr);
    e->swapchain = VK_NULL_HANDLE;
    
    std::printf("✅ Swapchain destroyed\n");
}

void create_swapchain(Engine* e, uint32_t width, uint32_t height) {
    std::printf("🔧 Creating swapchain %ux%u...\n", width, height);

    // Get surface capabilities and clamp extents
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(e->physicalDevice, e->surface, &caps));
    
    // Handle minimize case
    if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0) {
        std::printf("⚠️  Window minimized, deferring swapchain creation\n");
        return;
    }
    
    width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
    height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
    
    std::printf("📏 Clamped extents: %ux%u\n", width, height);

    vkb::SwapchainBuilder swapchainBuilder{e->physicalDevice, e->device, e->surface};
    
    // FIX FORMAT ISSUE: Use consistent format (UNORM for both)
    VkFormat targetFormat = VK_FORMAT_B8G8R8A8_UNORM;
    
    auto vkbSwapchain = swapchainBuilder
        .set_desired_format(VkSurfaceFormatKHR{ .format = targetFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(width, height)
        .add_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build();

    if (!vkbSwapchain) {
        std::printf("❌ FATAL: Failed to create swapchain!\n");
        std::exit(1);
    }

    e->swapchain = vkbSwapchain->swapchain;
    e->swapchainImages = vkbSwapchain->get_images().value();
    e->swapchainImageViews = vkbSwapchain->get_image_views().value();
    e->swapchainImageFormat = vkbSwapchain->image_format;
    e->swapchainExtent = vkbSwapchain->extent;

    // Create semaphores for each swapchain image
    e->imageAvailableSemaphores.resize(e->swapchainImages.size());
    e->renderFinishedSemaphores.resize(e->swapchainImages.size());
    
    VkSemaphoreCreateInfo semInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };
    
    for (size_t i = 0; i < e->swapchainImages.size(); i++) {
        VK_CHECK(vkCreateSemaphore(e->device, &semInfo, nullptr, &e->imageAvailableSemaphores[i]));
        VK_CHECK(vkCreateSemaphore(e->device, &semInfo, nullptr, &e->renderFinishedSemaphores[i]));
    }

    // Update memory stats
    e->memoryStats.swapchainMemoryBytes = 0;
    for (auto& image : e->swapchainImages) {
        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(e->device, image, &memReq);
        e->memoryStats.swapchainMemoryBytes += memReq.size;
    }
    e->memoryStats.totalMemoryBytes = e->memoryStats.swapchainMemoryBytes + 
                                     e->memoryStats.imageMemoryBytes + 
                                     e->memoryStats.bufferMemoryBytes;

    std::printf("✅ Swapchain created with %zu images\n", e->swapchainImages.size());
}
void init_swapchain(Engine* e, uint32_t width, uint32_t height) {
    vkb::SwapchainBuilder swapchainBuilder{ e->physicalDevice, e->device, e->surface };
    
    vkb::Swapchain vkbSwapchain = swapchainBuilder
        .set_desired_format(VkSurfaceFormatKHR{ .format = e->swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(width, height)
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build()
        .value();

    e->swapchainExtent = vkbSwapchain.extent;
    e->swapchain = vkbSwapchain.swapchain;
    e->swapchainImages = vkbSwapchain.get_images().value();
    e->swapchainImageViews = vkbSwapchain.get_image_views().value();

    e->swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    
    e->memoryStats = {};

    for (auto& image : e->swapchainImages) {
        if (image != VK_NULL_HANDLE) {
            VkMemoryRequirements memReq;
            vkGetImageMemoryRequirements(e->device, image, &memReq);
            e->memoryStats.swapchainMemoryBytes += memReq.size;
            e->memoryStats.totalMemoryBytes += memReq.size;
        }
    }

    e->imageAvailableSemaphores.resize(e->swapchainImages.size());
    e->renderFinishedSemaphores.resize(e->swapchainImages.size());
    
    VkSemaphoreCreateInfo semInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0
    };
    
    for (size_t i = 0; i < e->swapchainImages.size(); i++) {
        VK_CHECK(vkCreateSemaphore(e->device, &semInfo, nullptr, &e->imageAvailableSemaphores[i]));
        VK_CHECK(vkCreateSemaphore(e->device, &semInfo, nullptr, &e->renderFinishedSemaphores[i]));
    }

    VkExtent3D drawImageExtent = { 1200, 720, 1 };
    e->drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    e->drawImage.imageExtent = drawImageExtent;

    e->drawExtent = { drawImageExtent.width, drawImageExtent.height };

    VkImageUsageFlags drawImageUsages{};
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkImageCreateInfo rimg_info = image_create_info(e->drawImage.imageFormat, drawImageUsages, drawImageExtent);

    VmaAllocationCreateInfo rimg_allocinfo = {};
    rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(vmaCreateImage(e->allocator, &rimg_info, &rimg_allocinfo, &e->drawImage.image, &e->drawImage.allocation, nullptr));

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(e->device, e->drawImage.image, &memReq);
    e->memoryStats.imageMemoryBytes += memReq.size;
    e->memoryStats.totalMemoryBytes += memReq.size;

    VkImageViewCreateInfo rview_info = imageview_create_info(e->drawImage.imageFormat, e->drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
    VkComponentMapping mapping = { 
        .r = VK_COMPONENT_SWIZZLE_IDENTITY, 
        .g = VK_COMPONENT_SWIZZLE_IDENTITY, 
        .b = VK_COMPONENT_SWIZZLE_IDENTITY, 
        .a = VK_COMPONENT_SWIZZLE_IDENTITY 
    };
    rview_info.components = mapping;
    VK_CHECK(vkCreateImageView(e->device, &rview_info, nullptr, &e->drawImage.imageView));

    e->mainDeletionQueue.push_function([=]() {

        destroy_draw_image(e);
        for (size_t i = 0; i < e->imageAvailableSemaphores.size(); i++) {
            vkDestroySemaphore(e->device, e->imageAvailableSemaphores[i], nullptr);
            vkDestroySemaphore(e->device, e->renderFinishedSemaphores[i], nullptr);
        }
    });
    std::printf("Swapchain initialized with %zu images\n", e->swapchainImages.size());
}

void resize_swapchain(Engine* e) {
    if (!e->resize_requested) return;
    
    std::printf("🔄 RESIZE SWAPCHAIN: %ux%u\n", e->swapchainExtent.width, e->swapchainExtent.height);
    
    // Store the requested size
    uint32_t newWidth = e->swapchainExtent.width;
    uint32_t newHeight = e->swapchainExtent.height;
    
    // Reset flag immediately to prevent recursion
    e->resize_requested = false;
    
    // Complete GPU sync - CRITICAL!
    vkDeviceWaitIdle(e->device);
    
    // Destroy old swapchain AND draw image
    destroy_swapchain(e);
    destroy_draw_image(e);
    
    // Create new swapchain AND draw image
    create_swapchain(e, newWidth, newHeight);
    create_draw_image(e, newWidth, newHeight); // RESIZE DRAW IMAGE TOO!
    
    std::printf("✅ Resize complete: %ux%u\n", e->swapchainExtent.width, e->swapchainExtent.height);
}
// ---------------------Dynamic Rendering---------------------

VkRenderingAttachmentInfo attachment_info(VkImageView view, VkClearValue* clear, VkImageLayout layout)
{
    VkRenderingAttachmentInfo colorAttachment {};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.pNext = nullptr;
    colorAttachment.imageView = view;
    colorAttachment.imageLayout = layout;
    colorAttachment.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    if (clear) {
        colorAttachment.clearValue = *clear;
    }
    return colorAttachment;
}

// ------------------------- Commands -------------------------
void init_commands(Engine* e) {
    VkCommandPoolCreateInfo commandPoolInfo{};
    commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolInfo.queueFamilyIndex = e->graphicsQueueFamily;

    for (uint32_t i = 0; i < FRAME_OVERLAP; i++) {
        VK_CHECK(vkCreateCommandPool(e->device, &commandPoolInfo, nullptr, &e->frames[i].commandPool));

        VkCommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAllocInfo.commandPool = e->frames[i].commandPool;
        cmdAllocInfo.commandBufferCount = 1;
        cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        VK_CHECK(vkAllocateCommandBuffers(e->device, &cmdAllocInfo, &e->frames[i].mainCommandBuffer));
    }

    VK_CHECK(vkCreateCommandPool(e->device, &commandPoolInfo, nullptr, &e->immCommandPool));

    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = e->immCommandPool;
    cmdAllocInfo.commandBufferCount = 1;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    VK_CHECK(vkAllocateCommandBuffers(e->device, &cmdAllocInfo, &e->immCommandBuffer));

    e->mainDeletionQueue.push_function([=]() {
        vkDestroyCommandPool(e->device, e->immCommandPool, nullptr);
    });
    
    std::printf("Commands initialized\n");
}

// ------------------------- Sync -------------------------
VkFenceCreateInfo fence_create_info(VkFenceCreateFlags flags) {
    VkFenceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    info.flags = flags;
    return info;
}

VkSemaphoreCreateInfo semaphore_create_info(VkSemaphoreCreateFlags flags) {
    VkSemaphoreCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    info.flags = flags;
    return info;
}

void init_sync_structures(Engine* e) {
    VkFenceCreateInfo fenceCreateInfo = fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    VkSemaphoreCreateInfo semaphoreCreateInfo = semaphore_create_info(0);

    for (uint32_t i = 0; i < FRAME_OVERLAP; i++) {
        VK_CHECK(vkCreateFence(e->device, &fenceCreateInfo, nullptr, &e->frames[i].renderFence));
        VK_CHECK(vkCreateSemaphore(e->device, &semaphoreCreateInfo, nullptr, &e->frames[i].swapchainSemaphore));
        VK_CHECK(vkCreateSemaphore(e->device, &semaphoreCreateInfo, nullptr, &e->frames[i].renderSemaphore));
    }

    VK_CHECK(vkCreateFence(e->device, &fenceCreateInfo, nullptr, &e->immFence));
    e->mainDeletionQueue.push_function([=]() { vkDestroyFence(e->device, e->immFence, nullptr); });
    std::printf("Sync structures initialized\n");
}

// ----------------------------- Descriptors -------------------------------
void init_descriptors(Engine* e) {
    std::vector<DescriptorAllocator::PoolSizeRatio> sizes = {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}
    };

    e->globalDescriptorAllocator.init_pool(e->device, 10, sizes);

    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        e->drawImageDescriptorLayout = builder.build(e->device, VK_SHADER_STAGE_COMPUTE_BIT);
    }

    e->drawImageDescriptors = e->globalDescriptorAllocator.allocate(e->device, e->drawImageDescriptorLayout);

    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imgInfo.imageView = e->drawImage.imageView;

    VkWriteDescriptorSet drawImageWrite = {};
    drawImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    drawImageWrite.pNext = nullptr;
    drawImageWrite.dstBinding = 0;
    drawImageWrite.dstSet = e->drawImageDescriptors;
    drawImageWrite.descriptorCount = 1;
    drawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    drawImageWrite.pImageInfo = &imgInfo;

    vkUpdateDescriptorSets(e->device, 1, &drawImageWrite, 0, nullptr);

    e->mainDeletionQueue.push_function([e]() {
        e->globalDescriptorAllocator.destroy_pool(e->device);
        vkDestroyDescriptorSetLayout(e->device, e->drawImageDescriptorLayout, nullptr);
    });
}

void init_background_pipelines(Engine* e) {
    std::cout << "🛡️ 1. Starting pipeline initialization..." << std::endl;
    
    if (e == nullptr) {
        std::cout << "❌ ENGINE IS NULL!" << std::endl;
        std::exit(1);
    }
    if (e->device == VK_NULL_HANDLE) {
        std::cout << "❌ DEVICE IS NULL!" << std::endl;
        std::exit(1);
    }
    std::cout << "✅ Engine and device are valid" << std::endl;

    std::cout << "🛡️ 2. Loading shader..." << std::endl;
    VkShaderModule computeDrawShader;
    if(!e->util.load_shader_module("shaders/gradient.comp.spv", e->device, &computeDrawShader)) {
        std::cout << "❌ SHADER LOAD FAILED!" << std::endl;
        std::exit(1);
    }

    VkShaderModule skyShader;
    if(!e->util.load_shader_module("shaders/sky.comp.spv", e->device, &skyShader)) {
        std::cout << "❌ SHADER LOAD FAILED!" << std::endl;
        std::exit(1);
    }
    
    std::cout << "✅ Shader loaded successfully!" << std::endl;

    constexpr uint32_t SHADER_PUSH_CONSTANT_SIZE = 64;
    std::cout << "🛡️ 3. Creating pipeline layout..." << std::endl;
    VkPushConstantRange push_constant_range = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = SHADER_PUSH_CONSTANT_SIZE
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &e->drawImageDescriptorLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range
    };

    if (e->drawImageDescriptorLayout == VK_NULL_HANDLE) {
        std::cout << "❌ DESCRIPTOR LAYOUT IS NULL!" << std::endl;
        std::exit(1);
    }

    VkResult layoutResult = vkCreatePipelineLayout(e->device, &pipelineLayoutInfo, nullptr, &e->gradientPipelineLayout);
    if (layoutResult != VK_SUCCESS) {
        std::cout << "❌ PIPELINE LAYOUT CREATION FAILED: " << layoutResult << std::endl;
        std::exit(1);
    }
    std::cout << "✅ Pipeline layout created!" << std::endl;

    std::cout << "🛡️ 4. Creating compute pipeline..." << std::endl;
    VkPipelineShaderStageCreateInfo stageinfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = computeDrawShader,
        .pName = "main"
    };

    VkComputePipelineCreateInfo computePipelineCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = stageinfo,
        .layout = e->gradientPipelineLayout
    };

    VkResult pipelineResult = vkCreateComputePipelines(e->device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &e->gradientPipeline);
    if (pipelineResult != VK_SUCCESS) {
        std::cout << "❌ GRADIENT PIPELINE CREATION FAILED: " << pipelineResult << std::endl;
        std::exit(1);
    }

    computePipelineCreateInfo.stage.module = skyShader;
    VkPipeline skyPipeline;
    VkResult skyPipelineResult = vkCreateComputePipelines(e->device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &skyPipeline);
    if (skyPipelineResult != VK_SUCCESS) {
        std::cout << "❌ SKY PIPELINE CREATION FAILED: " << skyPipelineResult << std::endl;
        std::exit(1);
    }

    ComputeEffect gradient;
    gradient.pipeline = e->gradientPipeline;
    gradient.layout = e->gradientPipelineLayout;
    gradient.name = "gradient";
    gradient.data = {};
    gradient.data.data1 = glm::vec4(1, 0, 0, 1);
    gradient.data.data2 = glm::vec4(0, 0, 1, 1);

    ComputeEffect sky;
    sky.pipeline = skyPipeline;
    sky.layout = e->gradientPipelineLayout;
    sky.name = "sky";
    sky.data = {};
    sky.data.data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);

    e->backgroundEffects.push_back(gradient);
    e->backgroundEffects.push_back(sky);

    std::cout << "✅ Compute pipelines created!" << std::endl;

    e->mainDeletionQueue.push_function([=]() {
        std::cout << "Cleaning up pipelines..." << std::endl;
        
        for (auto& effect : e->backgroundEffects) {
            if (effect.pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(e->device, effect.pipeline, nullptr);
                effect.pipeline = VK_NULL_HANDLE;
            }
        }
        
        if (e->gradientPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(e->device, e->gradientPipelineLayout, nullptr);
            e->gradientPipelineLayout = VK_NULL_HANDLE;
        }
        
        e->backgroundEffects.clear();
        
        std::cout << "Pipeline cleanup complete!" << std::endl;
    });

    vkDestroyShaderModule(e->device, computeDrawShader, nullptr);
    vkDestroyShaderModule(e->device, skyShader, nullptr);
    
    std::cout << "✅ Pipeline initialization complete!" << std::endl;
}

void init_mesh_pipelines(Engine* e)
{
    VkShaderModule triangleFragShader;
    if (!e->util.load_shader_module("shaders/triangle_frag.frag.spv", e->device, &triangleFragShader)) {
        std::cout << "Failed to load triangle fragment shader\n";
        std::exit(1);
    }

    VkShaderModule triangleVertShader;
    if (!e->util.load_shader_module("shaders/colored_triangle_mesh.vert.spv", e->device, &triangleVertShader)) {
        std::cout << "Failed to load triangle vertex shader\n";
        std::exit(1);
    }

    VkPushConstantRange bufferRange{};
    bufferRange.offset = 0;
    bufferRange.size = sizeof(GPUDrawPushConstants);
    bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkPipelineLayoutCreateInfo pipeline_layout_info = e->util.pipeline_layout_create_info();
    pipeline_layout_info.pPushConstantRanges = &bufferRange;
    pipeline_layout_info.pushConstantRangeCount = 1;
    
    if (vkCreatePipelineLayout(e->device, &pipeline_layout_info, nullptr, &e->meshPipelineLayout) != VK_SUCCESS) {
        std::cerr << "Failed to create mesh pipeline layout\n";
        std::exit(1);
    }

    PipelineBuilder pbuilder = {};
    pbuilder.pipelineLayout = e->meshPipelineLayout;

    set_shaders(triangleVertShader, triangleFragShader, pbuilder);
    set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, pbuilder);
    set_polygon_mode(VK_POLYGON_MODE_FILL, pbuilder);
    set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE, pbuilder);
    set_multisampling_none(pbuilder);
    enable_blending_additive(pbuilder);
    disable_depthtest(pbuilder);

    set_color_attachment_format(e->drawImage.imageFormat, pbuilder);
    set_depth_format(VK_FORMAT_UNDEFINED, pbuilder);

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescriptions[2] = {};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, position);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, color);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;


    e->meshPipeline = build_pipeline(e->device, pbuilder);
    if (e->meshPipeline == VK_NULL_HANDLE) {
        std::cerr << "❌ Failed to create mesh pipeline. Aborting." << std::endl;
        std::exit(1);
    }

    vkDestroyShaderModule(e->device, triangleVertShader, nullptr);
    vkDestroyShaderModule(e->device, triangleFragShader, nullptr);

    e->mainDeletionQueue.push_function([=]() {
        vkDestroyPipelineLayout(e->device, e->meshPipelineLayout, nullptr);
        vkDestroyPipeline(e->device, e->meshPipeline, nullptr);
    });
}

// ------------------------- Helpers -------------------------
VkCommandBufferBeginInfo command_buffer_info(VkCommandBufferUsageFlags flags) {
    VkCommandBufferBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.flags = flags;
    return info;
}

VkSemaphoreSubmitInfo semaphore_submit_info(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore) {
    VkSemaphoreSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    submitInfo.semaphore = semaphore;
    submitInfo.stageMask = stageMask;
    submitInfo.deviceIndex = 0;
    submitInfo.value = 1;
    return submitInfo;
}

VkCommandBufferSubmitInfo command_buffer_submit_info(VkCommandBuffer cmd) {
    VkCommandBufferSubmitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    info.commandBuffer = cmd;
    info.deviceMask = 0;
    return info;
}

VkSubmitInfo2 submit_info(VkCommandBufferSubmitInfo* cmd,
                         VkSemaphoreSubmitInfo* signalSemaphoreInfo,
                         VkSemaphoreSubmitInfo* waitSemaphoreInfo) {
    VkSubmitInfo2 info{};
    info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    info.waitSemaphoreInfoCount = (waitSemaphoreInfo == nullptr) ? 0u : 1u;
    info.pWaitSemaphoreInfos   = waitSemaphoreInfo;
    info.signalSemaphoreInfoCount = (signalSemaphoreInfo == nullptr) ? 0u : 1u;
    info.pSignalSemaphoreInfos    = signalSemaphoreInfo;
    info.commandBufferInfoCount = 1;
    info.pCommandBufferInfos    = cmd;
    return info;
}

FrameData& get_current_frame() {
    return engine->frames[engine->frameNumber % FRAME_OVERLAP];
}

void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function, Engine* e)
{

    VK_CHECK(vkResetFences(e->device, 1, &e->immFence));
    VK_CHECK(vkResetCommandBuffer(e->immCommandBuffer, 0));

    VkCommandBuffer cmd = e->immCommandBuffer;

    VkCommandBufferBeginInfo cmdBeginInfo = command_buffer_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    function(cmd);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdinfo = command_buffer_submit_info(cmd);
    VkSubmitInfo2 submit = submit_info(&cmdinfo, nullptr, nullptr);

    VK_CHECK(vkQueueSubmit2(e->graphicsQueue, 1, &submit, e->immFence));

    VK_CHECK(vkWaitForFences(e->device, 1, &e->immFence, true, 9999999999));
}

void draw_geometry(Engine* e, VkCommandBuffer cmd)
{
    VkClearValue clearValue;
    clearValue.color = { { 0.0f, 0.0f, 0.0f, 0.0f } }; // Transparent clear to preserve compute shader output
    VkRenderingAttachmentInfo colorAttachment = attachment_info(e->drawImage.imageView, &clearValue, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Preserve compute shader output
    VkRenderingInfo renderInfo = e->util.rendering_info(e->drawExtent, &colorAttachment, nullptr);

    vkCmdBeginRendering(cmd, &renderInfo);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e->meshPipeline);

    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = static_cast<float>(e->drawExtent.width);
    viewport.height = static_cast<float>(e->drawExtent.height);
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = e->drawExtent.width;
    scissor.extent.height = e->drawExtent.height;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    GPUDrawPushConstants push_constants;
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 projection = glm::perspective(glm::radians(70.f), 
                                           static_cast<float>(e->drawExtent.width) / static_cast<float>(e->drawExtent.height), 
                                           0.1f, 100.0f);
    projection[1][1] *= -1;
    push_constants.worldMatrix = projection * view;

    // Draw rectangle
    if (e->rectangle.vertexBuffer.buffer != VK_NULL_HANDLE) {
        push_constants.vertexBuffer = e->rectangle.vertexBufferAddress;
        vkCmdPushConstants(cmd, e->meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);
        vkCmdBindVertexBuffers(cmd, 0, 1, &e->rectangle.vertexBuffer.buffer, std::array<VkDeviceSize, 1>{0}.data());
        if (e->rectangle.indexBuffer.buffer != VK_NULL_HANDLE) {
            vkCmdBindIndexBuffer(cmd, e->rectangle.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);
        } else {
            vkCmdDraw(cmd, 4, 1, 0, 0);
            std::cerr << "⚠️ Rectangle index buffer is invalid, using non-indexed draw" << std::endl;
        }
    } else {
        std::cerr << "❌ Rectangle vertex buffer is invalid!" << std::endl;
    }

    // Draw monkey mesh

    if (!e->testMeshes.empty() && e->testMeshes.size() > 2 && e->testMeshes[2]->meshBuffers.vertexBuffer.buffer != VK_NULL_HANDLE) {
        push_constants.vertexBuffer = e->testMeshes[2]->meshBuffers.vertexBufferAddress;
        vkCmdPushConstants(cmd, e->meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);
        vkCmdBindVertexBuffers(cmd, 0, 1, &e->testMeshes[2]->meshBuffers.vertexBuffer.buffer, std::array<VkDeviceSize, 1>{0}.data());
        if (e->testMeshes[2]->meshBuffers.indexBuffer.buffer != VK_NULL_HANDLE) {
            vkCmdBindIndexBuffer(cmd, e->testMeshes[2]->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, e->testMeshes[2]->surfaces[0].count, 1, 
                           e->testMeshes[2]->surfaces[0].startIndex, 0, 0);
        } else {
            std::cerr << "❌ Monkey mesh index buffer is invalid!" << std::endl;
        }
    } else {
        std::cerr << "❌ Monkey mesh is invalid or not loaded at index 2!" << std::endl;
    }

    vkCmdEndRendering(cmd);

}

void init_default_data(Engine* e)
{
    std::vector<Vertex> rect_vertices(4);
    rect_vertices[0].position = { 0.5f, -0.5f, 0.0f };
    rect_vertices[1].position = { 0.5f,  0.5f, 0.0f };
    rect_vertices[2].position = {-0.5f, -0.5f, 0.0f };
    rect_vertices[3].position = {-0.5f,  0.5f, 0.0f };
    rect_vertices[0].color = { 0.0f, 0.0f, 0.0f, 1.0f };
    rect_vertices[1].color = { 0.5f, 0.5f, 0.5f, 1.0f };
    rect_vertices[2].color = { 1.0f, 0.0f, 0.0f, 1.0f };
    rect_vertices[3].color = { 1.0f, 1.0f, 1.0f, 1.0f };

    std::vector<uint32_t> rect_indices = {
        0, 1, 2,
        2, 3, 0
    };

    e->rectangle = uploadMesh(e, rect_indices, rect_vertices);

    e->testMeshes = loadgltfMeshes(e, "assets/basicmesh.glb").value();
    
    // Debug mesh loading
    std::cout << "Loaded " << e->testMeshes.size() << " meshes from basicmesh.glb" << std::endl;
    for (size_t i = 0; i < e->testMeshes.size(); ++i) {
        std::cout << "Mesh " << i << ": vertexBuffer=" << e->testMeshes[i]->meshBuffers.vertexBuffer.buffer
                  << ", indexBuffer=" << e->testMeshes[i]->meshBuffers.indexBuffer.buffer
                  << ", indexCount=" << e->testMeshes[i]->surfaces[0].count << std::endl;
    }

    e->mainDeletionQueue.push_function([=]() {
        destroy_buffer(e->rectangle.vertexBuffer, e);
        destroy_buffer(e->rectangle.indexBuffer, e);
        for (auto& mesh : e->testMeshes) {
            if (mesh->meshBuffers.vertexBuffer.buffer != VK_NULL_HANDLE) {
                destroy_buffer(mesh->meshBuffers.vertexBuffer, e);
            }
            if (mesh->meshBuffers.indexBuffer.buffer != VK_NULL_HANDLE) {
                destroy_buffer(mesh->meshBuffers.indexBuffer, e);
            }
        }
    });
}

void draw_background(VkCommandBuffer cmd, Engine* e) {
    ComputeEffect& effect = e->backgroundEffects[e->currentBackgroundEffect];
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            e->gradientPipelineLayout, 0, 1,
                            &e->drawImageDescriptors, 0, nullptr);

    vkCmdPushConstants(cmd, e->gradientPipelineLayout,

                     VK_SHADER_STAGE_COMPUTE_BIT,
                     0, sizeof(PushConstants), &effect.data);

    uint32_t dispatchX = (e->drawImage.imageExtent.width + 15) / 16;
    uint32_t dispatchY = (e->drawImage.imageExtent.height + 15) / 16;
    vkCmdDispatch(cmd, dispatchX, dispatchY, 1);
}

void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView, Engine* e)
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    
    debug_ui_render();
    
    ImGui::Render();

    VkRenderingAttachmentInfo colorAttachment = attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderInfo = e->util.rendering_info(e->swapchainExtent, &colorAttachment, nullptr);

    vkCmdBeginRendering(cmd, &renderInfo);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRendering(cmd);
}


void engine_draw_frame(Engine* e) {
    // Handle resize BEFORE anything else
    if (e->resize_requested) {
        resize_swapchain(e);
        // If we just resized, skip this frame to avoid sync issues
        if (e->resize_requested) return;
    }
    
    // If swapchain is invalid (minimized), skip frame
    if (e->swapchain == VK_NULL_HANDLE || e->swapchainImages.empty()) {
        return;
    }

    FrameData& frame = get_current_frame();

    // Wait for previous frame to complete
    VK_CHECK(vkWaitForFences(e->device, 1, &frame.renderFence, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(e->device, 1, &frame.renderFence));
    VK_CHECK(vkResetCommandPool(e->device, frame.commandPool, 0));

    uint32_t swapchainImageIndex;
    VkResult acquireResult = vkAcquireNextImageKHR(
        e->device,
        e->swapchain,
        UINT64_MAX,
        frame.swapchainSemaphore,  // Use frame semaphore, not global
        VK_NULL_HANDLE,
        &swapchainImageIndex
    );

    // Handle swapchain out-of-date immediately
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR) {
        e->resize_requested = true;
        std::printf("🔄 Acquire indicated resize needed\n");
        return;
    } else if (acquireResult != VK_SUCCESS) {
        std::printf("❌ Acquire failed: %d\n", acquireResult);
        return;
    }

    VkCommandBuffer cmd = frame.mainCommandBuffer;
    VK_CHECK(vkResetCommandBuffer(cmd, 0));
    
    VkCommandBufferBeginInfo cmdBeginInfo = command_buffer_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    // ... thy existing rendering code here (UNCHANGED) ...
    transition_image(cmd, e->drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    draw_background(cmd, e);
    transition_image(cmd, e->drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    draw_geometry(e, cmd);
    transition_image(cmd, e->drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    transition_image(cmd, e->swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copy_image_to_image(cmd, e->drawImage.image, e->swapchainImages[swapchainImageIndex],
                        VkExtent3D{e->drawImage.imageExtent.width, e->drawImage.imageExtent.height, 1},
                        e->swapchainExtent);
    transition_image(cmd, e->swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    draw_imgui(cmd, e->swapchainImageViews[swapchainImageIndex], e);
    transition_image(cmd, e->swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    VK_CHECK(vkEndCommandBuffer(cmd));

    // Submit using FRAME semaphores, not global ones
    VkCommandBufferSubmitInfo cmdInfo = command_buffer_submit_info(cmd);
    VkSemaphoreSubmitInfo waitInfo = semaphore_submit_info(
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        frame.swapchainSemaphore  // Wait on acquire
    );
    VkSemaphoreSubmitInfo signalInfo = semaphore_submit_info(
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        frame.renderSemaphore     // Signal when done
    );

    VkSubmitInfo2 submit = submit_info(&cmdInfo, &signalInfo, &waitInfo);
    VK_CHECK(vkQueueSubmit2(e->graphicsQueue, 1, &submit, frame.renderFence));

    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &frame.renderSemaphore;  // Wait on render complete
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &e->swapchain;
    presentInfo.pImageIndices = &swapchainImageIndex;

    VkResult presentResult = vkQueuePresentKHR(e->graphicsQueue, &presentInfo);
    
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        e->resize_requested = true;
        std::printf("🔄 Present indicated resize needed\n");
    } else if (presentResult != VK_SUCCESS) {
        std::printf("❌ Present failed: %d\n", presentResult);
    }

    e->frameNumber++;
}

void engine_cleanup(Engine* e) {
    if (!e) return;

    vkDeviceWaitIdle(e->device);

    for (uint32_t i = 0; i < FRAME_OVERLAP; i++) {
        e->frames[i].deletionQueue.flush();
        vkDestroyCommandPool(e->device, e->frames[i].commandPool, nullptr);
        vkDestroySemaphore(e->device, e->frames[i].swapchainSemaphore, nullptr);
        vkDestroySemaphore(e->device, e->frames[i].renderSemaphore, nullptr);
        vkDestroyFence(e->device, e->frames[i].renderFence, nullptr);
    }

    e->mainDeletionQueue.flush();

    for (auto view : e->swapchainImageViews) {
        vkDestroyImageView(e->device, view, nullptr);
    }

    if (e->swapchain) vkDestroySwapchainKHR(e->device, e->swapchain, nullptr);
    if (e->device) vkDestroyDevice(e->device, nullptr);

    if (e->debug_messenger) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(e->instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func) {
            func(e->instance, e->debug_messenger, nullptr);
        }
        e->debug_messenger = VK_NULL_HANDLE;
    }

    if (e->surface) vkDestroySurfaceKHR(e->instance, e->surface, nullptr);
    if (e->instance) vkDestroyInstance(e->instance, nullptr);
    if (e->window) glfwDestroyWindow(e->window);
    
    glfwTerminate();

    std::cout << "Engine cleanup complete!" << std::endl;
}
