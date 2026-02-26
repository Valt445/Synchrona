#include "engine.h"
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
#include <glm/packing.hpp>
#include "texture_loader.h"
// Define the global engine pointer declared as `extern` in the header

Engine* engine = nullptr;

// ------------------------- Initialization -------------------------

// Initialize the Engine
void init(Engine* e, uint32_t x, uint32_t y) {
    *e = Engine{};
    ::engine = e;

    init_vulkan(e);
    // Let init_swapchain() pick and set e->swapchainImageFormat consistently.
    init_swapchain(e, x, y);
    init_descriptors(e);
    create_draw_image(e, x, y);
    init_commands(e);
    init_sync_structures(e);
    init_pipelines(e);
    init_mesh_pipelines(e);
    init_default_data(e);
    init_imgui(e);

    // ── Camera setup ─────────────────────────────────────────────────────
    // Register all input callbacks (replaces the old key callback in init_vulkan).
    // Must be called AFTER init_vulkan sets the window user pointer to Engine*.
    setupCameraCallbacks(e->window);

    // Frame the loaded model. If you know your model's center/radius, pass them here.
    // These defaults work well for a car-sized object at the origin.
    e->mainCamera.focusOn(glm::vec3(0.0f, 0.5f, 0.0f), 5.0f);
    // Once you compute the real bounding box from your GLB, call:
    //   e->mainCamera.focusOn(bboxCenter, bboxRadius);
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
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    e->window = glfwCreateWindow(e->width, e->height, "Vulkan Window", nullptr, nullptr);
    if (!e->window) {
        std::printf("Failed to create GLFW window\n");
        std::exit(1);
    }
    glfwSetWindowUserPointer(e->window, e);
    // NOTE: The key callback is installed by setupCameraCallbacks() which is called
    // from init() after the window exists. It handles both engine->keys[] and camera
    // mode switches in one place. Do NOT add a separate key callback here.

    glfwSetFramebufferSizeCallback(e->window, [](GLFWwindow* window, int width, int height) {
        Engine* engine = static_cast<Engine*>(glfwGetWindowUserPointer(window));
        if (engine) {
            if (width == 0 || height == 0) {
                std::printf("📦 Window minimized\n");
                engine->resize_requested = true;
                return;
            }
            static int lastW = 0, lastH = 0;
            if (std::abs(width - lastW) > 2 || std::abs(height - lastH) > 2) {
                lastW = width;
                lastH = height;
                engine->swapchainExtent.width = width;
                engine->swapchainExtent.height = height;
                engine->resize_requested = true;
            }
        }
        });

    if (glfwCreateWindowSurface(e->instance, e->window, nullptr, &e->surface) != VK_SUCCESS) {
        std::printf("Failed to create Vulkan surface\n");
        std::exit(1);
    }

    VkPhysicalDeviceVulkan12Features features12{
         .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
         .pNext = nullptr, // Important: no more custom pNext chain here
         .descriptorIndexing = VK_TRUE,
         .shaderInputAttachmentArrayDynamicIndexing = VK_FALSE, // Add other required 1.2 booleans as needed
         .shaderUniformTexelBufferArrayDynamicIndexing = VK_FALSE,
         .shaderStorageTexelBufferArrayDynamicIndexing = VK_FALSE,
         .shaderUniformBufferArrayNonUniformIndexing = VK_FALSE,
         .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
         .shaderStorageBufferArrayNonUniformIndexing = VK_FALSE,
         .shaderStorageImageArrayNonUniformIndexing = VK_FALSE,
         .shaderInputAttachmentArrayNonUniformIndexing = VK_FALSE,
         .shaderUniformTexelBufferArrayNonUniformIndexing = VK_FALSE,
         .shaderStorageTexelBufferArrayNonUniformIndexing = VK_FALSE,
         .descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE,
         .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
         .descriptorBindingStorageImageUpdateAfterBind = VK_TRUE,
         .descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE,
         .descriptorBindingUniformTexelBufferUpdateAfterBind = VK_FALSE,
         .descriptorBindingUpdateUnusedWhilePending = VK_TRUE,
         .descriptorBindingPartiallyBound = VK_TRUE,
         .descriptorBindingVariableDescriptorCount = VK_TRUE,
         .runtimeDescriptorArray = VK_TRUE,
         .bufferDeviceAddress = VK_TRUE,

    };

    VkPhysicalDeviceVulkan13Features features13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = nullptr,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE
    };

    VkPhysicalDeviceFeatures coreFeatures{
        .shaderInt64 = VK_TRUE
    };

    vkb::PhysicalDeviceSelector selector{ vkb_inst };
    vkb::PhysicalDevice physicalDevice = selector
        .set_minimum_version(1, 3)
        .set_required_features(coreFeatures)
        .set_required_features_12(features12)
        .set_required_features_13(features13)
        .set_surface(e->surface)
        .select()
        .value();

    vkb::DeviceBuilder deviceBuilder{ physicalDevice };
    // If vkbDeviceBuilder needs pNext chain, it should pick it up from selector
    vkb::Device vkbDevice = deviceBuilder.build().value();

    e->device = vkbDevice.device;
    e->physicalDevice = physicalDevice.physical_device;
    e->graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    e->graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = e->physicalDevice;
    allocatorInfo.device = e->device;
    allocatorInfo.instance = e->instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &e->allocator));

    e->mainDeletionQueue.push_function([=]() {
        if (e->allocator) {
            vmaDestroyAllocator(e->allocator);
            e->allocator = VK_NULL_HANDLE;
        }
        });
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
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
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
    }
    else {
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
    if (e->defaultSamplerLinear != VK_NULL_HANDLE) {
        vkDestroySampler(e->device, e->defaultSamplerLinear, nullptr);
        e->defaultSamplerLinear = VK_NULL_HANDLE;
    }
    if (e->defaultSamplerNearest != VK_NULL_HANDLE) {
        vkDestroySampler(e->device, e->defaultSamplerNearest, nullptr);
        e->defaultSamplerNearest = VK_NULL_HANDLE;
    }
}

// --- CHANGED: create_image (no-data) ---
AllocatedImage create_image(Engine* e, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped)
{
    AllocatedImage newImage{};
    newImage.imageFormat = format;
    newImage.imageExtent = size;

    VkImageCreateInfo img_info = image_create_info(format, usage, size);
    if (mipmapped)
    {
        img_info.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;
    }

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(vmaCreateImage(e->allocator, &img_info, &alloc_info, &newImage.image, &newImage.allocation, nullptr));

    VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
    if (format == VK_FORMAT_D32_SFLOAT)
    {
        aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    VkImageViewCreateInfo view_info = imageview_create_info(format, newImage.image, aspectFlags);
    view_info.subresourceRange.levelCount = img_info.mipLevels;

    VK_CHECK(vkCreateImageView(e->device, &view_info, nullptr, &newImage.imageView));

    // Ownership: caller is responsible for destroying the image/imageView/allocation.
    // Do NOT register an automatic deletion lambda here to avoid double-free when
    // the caller also registers cleanup.
    return newImage;
}

// --- CHANGED: create_image (data upload) ---
AllocatedImage create_image(void* data, Engine* e, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped)
{
    size_t data_size = size.depth * size.height * size.width * 4; // 4 bytes per pixel for R8G8B8A8
    AllocatedBuffer uploadbuffer = create_buffer(
        e->allocator,
        data_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU
    );

    // Map explicitly and copy -- VMA may not provide pMappedData for this usage automatically.
    void* mapped = nullptr;
    VK_CHECK(vmaMapMemory(e->allocator, uploadbuffer.allocation, &mapped));
    memcpy(mapped, data, data_size);
    vmaUnmapMemory(e->allocator, uploadbuffer.allocation);

    // Fix: include TRANSFER_DST (was duplicated TRANSFER_SRC previously)
    AllocatedImage newImage = create_image(e, size, format, usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, mipmapped);

    immediate_submit([&](VkCommandBuffer cmd) {
        // Transition
        transition_image(cmd, newImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        // Copy
        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = size;

        vkCmdCopyBufferToImage(
            cmd,
            uploadbuffer.buffer,
            newImage.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &copyRegion
        );

        transition_image(cmd, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        }, e);

    destroy_buffer(uploadbuffer, e);
    return newImage;
}

void destroy_image(const AllocatedImage& image, Engine* e)
{
    if (image.imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(e->device, image.imageView, nullptr);
    }
    if (image.image != VK_NULL_HANDLE) {
        vmaDestroyImage(e->allocator, image.image, image.allocation);
    }
}

void create_draw_image(Engine* e, uint32_t width, uint32_t height) {
    // 1. Destroy old resources
    destroy_draw_image(e);

    // 2. Setup Samplers (Usually better to move these to a one-time init_samplers function,
    // but having them here works as long as you destroy them in destroy_draw_image)
    VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sampl.magFilter = VK_FILTER_NEAREST;
    sampl.minFilter = VK_FILTER_NEAREST;
    vkCreateSampler(e->device, &sampl, nullptr, &e->defaultSamplerNearest);

    sampl.magFilter = VK_FILTER_LINEAR;
    sampl.minFilter = VK_FILTER_LINEAR;
    vkCreateSampler(e->device, &sampl, nullptr, &e->defaultSamplerLinear);

    // 3. Image Creation
    VkExtent3D drawImageExtent = { width, height, 1 };
    e->drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    e->drawImage.imageExtent = drawImageExtent;
    e->drawExtent = { width, height };

    VkImageUsageFlags drawImageUsages{};
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT; // Needed for Binding 1
    drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_SAMPLED_BIT; // Needed for Binding 0

    VkImageCreateInfo rimg_info = image_create_info(e->drawImage.imageFormat, drawImageUsages, drawImageExtent);
    VmaAllocationCreateInfo rimg_allocinfo = { .usage = VMA_MEMORY_USAGE_GPU_ONLY };
    rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(vmaCreateImage(e->allocator, &rimg_info, &rimg_allocinfo, &e->drawImage.image, &e->drawImage.allocation, nullptr));

    // 4. Image View Creation
    VkImageViewCreateInfo rview_info = imageview_create_info(e->drawImage.imageFormat, e->drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK(vkCreateImageView(e->device, &rview_info, nullptr, &e->drawImage.imageView));

    // --- NEW: DUAL BINDING UPDATE ---

    // Info for Binding 0: Reading via Sampler
    VkDescriptorImageInfo samplerInfo{};
    samplerInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    samplerInfo.imageView = e->drawImage.imageView;
    samplerInfo.sampler = e->defaultSamplerLinear;

    // Info for Binding 1: Writing via Storage Image
    VkDescriptorImageInfo storageInfo{};
    storageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    storageInfo.imageView = e->drawImage.imageView;

    VkWriteDescriptorSet drawImageWrites[2] = {};

    // Binding 0: COMBINED_IMAGE_SAMPLER array (textures) - slot 0 = draw image sampler
    drawImageWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    drawImageWrites[0].dstSet = e->bindlessSet;
    drawImageWrites[0].dstBinding = 0;
    drawImageWrites[0].dstArrayElement = 0;
    drawImageWrites[0].descriptorCount = 1;
    drawImageWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    drawImageWrites[0].pImageInfo = &samplerInfo;

    // Binding 1: STORAGE_IMAGE (draw image for compute write)
    drawImageWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    drawImageWrites[1].dstSet = e->bindlessSet;
    drawImageWrites[1].dstBinding = 1;
    drawImageWrites[1].descriptorCount = 1;
    drawImageWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    drawImageWrites[1].pImageInfo = &storageInfo;

    vkUpdateDescriptorSets(e->device, 2, drawImageWrites, 0, nullptr);

    std::cout << "Draw image updated: Binding 0 (Sampler) and Binding 1 (Storage) synced.\n";
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

    vkb::SwapchainBuilder swapchainBuilder{ e->physicalDevice, e->device, e->surface };

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
    VkExtent3D depthExtent = { e->swapchainExtent.width, e->swapchainExtent.height, 1 };

    e->depthImage = create_image(e, depthExtent, VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, false);

    e->mainDeletionQueue.push_function([=]() {

        destroy_draw_image(e);
        destroy_image(e->depthImage, e);
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

    if (e->depthImage.image != VK_NULL_HANDLE) {
        destroy_image(e->depthImage, e);
        e->depthImage = {}; // Zero-initialize after destruction
    }

    // Create new swapchain AND draw image
    create_swapchain(e, newWidth, newHeight);
    create_draw_image(e, newWidth, newHeight); // RESIZE DRAW IMAGE TOO!
    VkExtent3D depthExtent = { newWidth, newHeight, 1 };
    e->depthImage = create_image(e, depthExtent, VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, false);
    std::printf("✅ Resize complete: %ux%u\n", e->swapchainExtent.width, e->swapchainExtent.height);
}
// ---------------------Dynamic Rendering---------------------

VkRenderingAttachmentInfo attachment_info(VkImageView view, VkClearValue* clear, VkImageLayout layout)
{
    VkRenderingAttachmentInfo colorAttachment{};
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
    std::cout << "Initializing high-performance bindless system (Dual-Mode)...\n";

    std::vector<DescriptorAllocator::PoolSizeRatio> sizes = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000.0f },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100.0f }
    };
    e->globalDescriptorAllocator.init_pool(e->device, 10, sizes);

    DescriptorLayoutBuilder builder;
    // Keep bindings matching what the shaders declare:
    // Binding 0: COMBINED_IMAGE_SAMPLER array (textures) - fragment shader "allTextures"
    // Binding 1: STORAGE_IMAGE (draw image) - compute shader "image"
    // We use a fixed count of 4096 for binding 0 — no VARIABLE_DESCRIPTOR_COUNT needed
    // (that flag is only valid on the LAST binding, which is the storage image here).
    builder.add_bindless_array(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096);
    builder.add_binding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1);

    e->bindlessLayout = builder.build(
        e->device,
        VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr,
        VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT
    );

    std::cout << "[BINDLESS] Layout created successfully\n";

    // Don't pass variableDescriptorCount — we use fixed counts on both bindings.
    e->bindlessSet = e->globalDescriptorAllocator.allocate(e->device, e->bindlessLayout, 0);

    std::cout << "[BINDLESS] Set allocated successfully: " << e->bindlessSet << "\n";

    e->mainDeletionQueue.push_function([=]() {
        vkDestroyDescriptorSetLayout(e->device, e->bindlessLayout, nullptr);
        vkFreeDescriptorSets(e->device, e->globalDescriptorAllocator.pool, 1, &e->bindlessSet);
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

    // ADD DEBUG OUTPUT FOR SHADER PATHS
    std::cout << "   Looking for: shaders/gradient.comp.spv" << std::endl;

    VkShaderModule computeDrawShader;
    auto loadResult1 = e->util.load_shader_module("shaders/gradient.comp.spv", e->device, &computeDrawShader);
    std::cout << "   Gradient shader load result: " << (loadResult1 ? "SUCCESS" : "FAILED") << std::endl;

    if (!loadResult1) {
        std::cout << "❌ GRADIENT SHADER LOAD FAILED!" << std::endl;
        std::exit(1);
    }

    std::cout << "   Looking for: shaders/sky.comp.spv" << std::endl;
    VkShaderModule skyShader;
    auto loadResult2 = e->util.load_shader_module("shaders/sky.comp.spv", e->device, &skyShader);
    std::cout << "   Sky shader load result: " << (loadResult2 ? "SUCCESS" : "FAILED") << std::endl;

    if (!loadResult2) {
        std::cout << "❌ SKY SHADER LOAD FAILED!" << std::endl;
        std::exit(1);
    }

    std::cout << "✅ Shaders loaded successfully!" << std::endl;


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
        .pSetLayouts = &e->bindlessLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range
    };

    if (e->bindlessLayout == VK_NULL_HANDLE) {
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
    gradient.effectData = {};
    gradient.effectData.data1 = glm::vec4(1, 0, 0, 1);
    gradient.effectData.data2 = glm::vec4(0, 0, 1, 1);

    ComputeEffect sky;
    sky.pipeline = skyPipeline;
    sky.layout = e->gradientPipelineLayout;
    sky.name = "sky";
    sky.effectData = {};
    sky.effectData.data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);

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
    std::cout << "Initializing mesh pipelines...\n";

    // 1. Load shaders
    VkShaderModule meshVertShader;
    if (!e->util.load_shader_module("shaders/colored_triangle_mesh.vert.spv", e->device, &meshVertShader)) {
        std::cerr << "Failed to load vertex shader\n"; std::exit(1);
    }

    VkShaderModule meshFragShader;
    if (!e->util.load_shader_module("shaders/tex_image.frag.spv", e->device, &meshFragShader)) {
        std::cerr << "Failed to load fragment shader\n"; std::exit(1);
    }

    // 2. Push constants - MUST match MeshPushConstants exactly (88 bytes)
    VkPushConstantRange pushRange{};
    pushRange.offset = 0;
    pushRange.size = sizeof(MeshPushConstants);   // 88 bytes
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // 3. Pipeline layout with bindless set
    VkPipelineLayoutCreateInfo layoutInfo = e->util.pipeline_layout_create_info();
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &e->bindlessLayout;   // ← important
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    VK_CHECK(vkCreatePipelineLayout(e->device, &layoutInfo, nullptr, &e->meshPipelineLayout));

    // 4. Build pipeline
    PipelineBuilder pb;
    set_shaders(meshVertShader, meshFragShader, pb);
    set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, pb);
    set_polygon_mode(VK_POLYGON_MODE_FILL, pb);
    set_cull_mode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, pb);
    set_multisampling_none(pb);
    enable_blending_alphablend(pb);
    enable_depthtest(pb, VK_COMPARE_OP_LESS_OR_EQUAL);

    set_color_attachment_format(e->drawImage.imageFormat, pb);
    set_depth_format(e->depthImage.imageFormat, pb);

    pb.pipelineLayout = e->meshPipelineLayout;

    // Empty vertex input (we use buffer reference)
    pb.vertexInputInfo = {};
    pb.vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    e->meshPipeline = build_pipeline(e->device, pb);
    if (e->meshPipeline == VK_NULL_HANDLE) {
        std::cerr << "❌ Failed to create mesh pipeline\n"; std::exit(1);
    }

    // Cleanup shaders
    vkDestroyShaderModule(e->device, meshVertShader, nullptr);
    vkDestroyShaderModule(e->device, meshFragShader, nullptr);

    e->mainDeletionQueue.push_function([=]() {
        vkDestroyPipelineLayout(e->device, e->meshPipelineLayout, nullptr);
        vkDestroyPipeline(e->device, e->meshPipeline, nullptr);
        });

    std::cout << "✅ Mesh pipeline created with bindless layout\n";
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
    info.pWaitSemaphoreInfos = waitSemaphoreInfo;
    info.signalSemaphoreInfoCount = (signalSemaphoreInfo == nullptr) ? 0u : 1u;
    info.pSignalSemaphoreInfos = signalSemaphoreInfo;
    info.commandBufferInfoCount = 1;
    info.pCommandBufferInfos = cmd;
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

void draw_geometry(Engine* e, VkCommandBuffer cmd) {
    // 1. Setup Rendering Info
    VkRenderingAttachmentInfo colorAttachment = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    colorAttachment.imageView = e->drawImage.imageView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Keep the compute stars/background
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingAttachmentInfo depthAttachment = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    depthAttachment.imageView = e->depthImage.imageView;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil.depth = 1.0f; // Clear to "Far"

    VkRenderingInfo renderInfo = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO };
    renderInfo.renderArea = { 0, 0, (uint32_t)e->drawExtent.width, (uint32_t)e->drawExtent.height };
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorAttachment;
    renderInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderInfo);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e->meshPipeline);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        e->meshPipelineLayout, 0, 1, &e->bindlessSet, 0, nullptr);
    // ── Camera matrices ───────────────────────────────────────────────────
    float aspect = (float)e->drawExtent.width / (float)e->drawExtent.height;
    glm::mat4 view = e->mainCamera.getViewMatrix();
    glm::mat4 projection = e->mainCamera.getProjectionMatrix(aspect);
    projection[1][1] *= -1;   // Vulkan Y-flip

    glm::mat4 viewProj = projection * view;

    // Dynamic State
    VkViewport viewport = { 0, 0, (float)e->drawExtent.width, (float)e->drawExtent.height, 0, 1 };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor = { {0, 0}, {e->drawExtent.width, e->drawExtent.height} };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    for (auto& asset : e->testMeshes) {
        for (auto& surface : asset->surfaces) {
            MeshPushConstants push;

            // Static model matrix — orbit the camera instead of spinning the model.
            // To add per-object transforms later, store them in MeshAsset.
            glm::mat4 model = glm::mat4(1.0f);

            push.worldMatrix = projection * view * model;
            push.vertexBuffer = asset->meshBuffers.vertexBufferAddress;
            push.textureIndex = surface.albedoTextureIndex;
            vkCmdPushConstants(cmd, e->meshPipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(MeshPushConstants), &push);

            vkCmdBindIndexBuffer(cmd, asset->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, surface.count, 1, surface.startIndex, 0, 0);
        }
    }

    vkCmdEndRendering(cmd);
}

void upload_texture_to_bindless(Engine* e, AllocatedImage img, VkSampler sampler, uint32_t index) {
    DescriptorWriter writer;
    // Write to binding 0, at the specific array index
    writer.write_image(0, img.imageView, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    // This uses the function in your descriptors.cpp to update ONLY that slot
    writer.update_set_at_index(e->device, e->bindlessSet, index);
}

void init_default_data(Engine* e)
{
    // Load GLTF meshes (includes the monkey head)
    auto testMeshes = loadgltfMeshes(e, "assets/ToyCar.glb");
    if (testMeshes.has_value()) {
        e->testMeshes = std::move(testMeshes.value());
        std::cout << "✓ Loaded " << e->testMeshes.size() << " meshes from GLTF (includes monkey)\n";
    }
    else {
        std::cout << "⚠️ No meshes loaded from GLTF file – monkey not found\n";
        e->testMeshes.clear();
    }


    // Create fallback 1x1 images (kept as safety)
    uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
    e->whiteImage = create_image((void*)&white, e, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
    upload_texture_to_bindless(e, e->whiteImage, e->defaultSamplerLinear, 1); // <-- add this

    uint32_t grey = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1));
    e->greyImage = create_image((void*)&grey, e, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
    upload_texture_to_bindless(e, e->whiteImage, e->defaultSamplerLinear, 1); // <-- add this

    uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 1));
    e->blackImage = create_image((void*)&black, e, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    upload_texture_to_bindless(e, e->whiteImage, e->defaultSamplerLinear, 1); // <-- add this

    // Gradient fallback
    std::array<uint32_t, 256> gradientPixels;
    for (int i = 0; i < 256; i++) {
        float t = i / 255.0f;
        gradientPixels[i] = glm::packUnorm4x8(glm::vec4(t, t, t, 1.0f));
    }
    e->errrorImage = create_image((void*)&gradientPixels, e,
        VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

    // Create samplers


    // Load MARBLE texture (replace with your marble image)
    AllocatedImage loadedTexture{};
    if (load_texture_from_file("assets/pavement.jpg", e, loadedTexture, e->defaultSamplerLinear)) {
        // Destroy fallback whiteImage and use marble texture
        if (e->whiteImage.image != VK_NULL_HANDLE) destroy_image(e->whiteImage, e);
        e->whiteImage = loadedTexture;
        std::cerr << "✅ Loaded assets/marble.jpg – monkey will be fancy marble!\n";
    }
    else {
        std::cerr << "⚠️ Could not load marble.jpg – using fallback white texture\n";
    }

    // Helper to update the descriptor set with the current texture
    auto update_single_image_descriptor = [&](Engine* e) {
        if (e->singleImageDescriptorSet == VK_NULL_HANDLE) return;
        VkDescriptorImageInfo setImgInfo{};
        setImgInfo.sampler = e->defaultSamplerLinear;
        setImgInfo.imageView = e->whiteImage.imageView;
        setImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet setWrite{};
        setWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        setWrite.dstSet = e->singleImageDescriptorSet;
        setWrite.dstBinding = 0;
        setWrite.dstArrayElement = 0;
        setWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        setWrite.descriptorCount = 1;
        setWrite.pImageInfo = &setImgInfo;

        vkUpdateDescriptorSets(e->device, 1, &setWrite, 0, nullptr);
        };

    // Allocate and update the single descriptor set for meshes
    if (e->singleImageDescriptorSetLayout != VK_NULL_HANDLE) {
        e->singleImageDescriptorSet = e->globalDescriptorAllocator.allocate(e->device, e->singleImageDescriptorSetLayout);
        update_single_image_descriptor(e);
    }
    else {
        std::cerr << "⚠️ singleImageDescriptorSetLayout is NULL – mesh texturing unavailable\n";
    }

    // Allocate ONCE and update with texture
    if (e->singleImageDescriptorSetLayout != VK_NULL_HANDLE) {
        e->meshTextureSet = e->globalDescriptorAllocator.allocate(
            e->device, e->singleImageDescriptorSetLayout
        );

        DescriptorWriter writer;
        writer.write_image(0, e->whiteImage.imageView, e->defaultSamplerLinear,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        writer.update_set(e->device, e->meshTextureSet);
    }

    e->mainDeletionQueue.push_function([=]() {
        // Destroy any loaded mesh buffers
        for (auto& mesh : e->testMeshes) {
            if (mesh->meshBuffers.vertexBuffer.buffer != VK_NULL_HANDLE) {
                destroy_buffer(mesh->meshBuffers.vertexBuffer, e);
            }
            if (mesh->meshBuffers.indexBuffer.buffer != VK_NULL_HANDLE) {
                destroy_buffer(mesh->meshBuffers.indexBuffer, e);
            }
        }

        // Destroy images
        destroy_image(e->whiteImage, e);
        destroy_image(e->greyImage, e);
        destroy_image(e->blackImage, e);
        destroy_image(e->errrorImage, e);

        // Destroy samplers
        if (e->defaultSamplerNearest != VK_NULL_HANDLE) {
            vkDestroySampler(e->device, e->defaultSamplerNearest, nullptr);
            e->defaultSamplerNearest = VK_NULL_HANDLE;
        }
        if (e->defaultSamplerLinear != VK_NULL_HANDLE) {
            vkDestroySampler(e->device, e->defaultSamplerLinear, nullptr);
            e->defaultSamplerLinear = VK_NULL_HANDLE;
        }
        });
}

void draw_background(VkCommandBuffer cmd, Engine* e) {
    ComputeEffect& effect = e->backgroundEffects[e->currentBackgroundEffect];

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

    // FIX: Bind 'bindlessSet', not 'drawImageDescriptors'
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        e->gradientPipelineLayout, 0, 1,
        &e->bindlessSet, 0, nullptr);

    vkCmdPushConstants(cmd,
        e->gradientPipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(ScenePushConstants),
        &effect.effectData);

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
    // ── Update camera first — computes delta time and FPS movement ─────────
    e->mainCamera.update(e->window);

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
    }
    else if (acquireResult != VK_SUCCESS) {
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
    VkImageMemoryBarrier2 depthBarrier = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    depthBarrier.image = e->depthImage.image;
    depthBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT; // Required for Depth formats
    depthBarrier.subresourceRange.levelCount = 1;
    depthBarrier.subresourceRange.layerCount = 1;

    VkDependencyInfo dep = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.pImageMemoryBarriers = &depthBarrier;
    dep.imageMemoryBarrierCount = 1;
    vkCmdPipelineBarrier2(cmd, &dep);

    draw_geometry(e, cmd);
    transition_image(cmd, e->drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    transition_image(cmd, e->swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copy_image_to_image(cmd, e->drawImage.image, e->swapchainImages[swapchainImageIndex],
        VkExtent3D{ e->drawImage.imageExtent.width, e->drawImage.imageExtent.height, 1 },
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
    }
    else if (presentResult != VK_SUCCESS) {
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

    // Destroy sceneTextures BEFORE mainDeletionQueue (which destroys the VMA allocator).
    // Destroying them after causes the VMA "allocations not freed" assertion.
    for (auto& tex : e->sceneTextures) {
        destroy_image(tex, e);
    }
    e->sceneTextures.clear();

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

    // sceneTextures already destroyed before mainDeletionQueue above.
    glfwTerminate();

    std::cout << "Engine cleanup complete!" << std::endl;
}