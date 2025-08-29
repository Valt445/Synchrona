#include "engine.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
#include <vk_video/vulkan_video_codec_av1std.h>
#include <vulkan/vulkan_core.h>
#include <VkBootstrap.h>
#include <GLFW/glfw3.h>
#include "images.h"
#include "helper.h"
#include "descriptors.h"
#include <iostream>

// Define the global engine pointer declared as `extern` in the header
Engine* engine = nullptr;

// ------------------------- Initialization -------------------------

// Initialize the Engine
void init(Engine* e, uint32_t x, uint32_t y) {
    // DO NOT use memset on a C++ type with std::vector members.
    *e = Engine{};         // value-initialize safely
    ::engine = e;          // set the global for get_current_frame()

    init_vulkan(e);
    init_swapchain(e, x, y);
    init_commands(e);
    init_sync_structures(e);
    init_descriptors(e);
    init_pipelines(e);
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

    // Initialize GLFW + window
    if (!glfwInit()) {
        std::printf("Failed to initialize GLFW\n");
        std::exit(1);
    }

    if (!glfwVulkanSupported()) {
        std::printf("GLFW reports Vulkan not supported!\n");
        std::exit(1);
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    e->window = glfwCreateWindow(800, 600, "Vulkan Window", nullptr, nullptr);
    if (!e->window) {
        std::printf("Failed to create GLFW window\n");
        std::exit(1);
    }

    // Create Vulkan surface
    if (glfwCreateWindowSurface(e->instance, e->window, nullptr, &e->surface) != VK_SUCCESS) {
        std::printf("Failed to create Vulkan surface\n");
        std::exit(1);
    }

    // Fixed field initialization order to silence warnings
    VkPhysicalDeviceVulkan13Features features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = nullptr,
        .synchronization2 = VK_TRUE,  // Fixed order
        .dynamicRendering = VK_TRUE
    };

    VkPhysicalDeviceVulkan12Features features12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = nullptr,
        .descriptorIndexing = VK_TRUE,  // Fixed order
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
    // --- FIX 1: Changed .flights to .flags ---
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &e->allocator));

    e->mainDeletionQueue.push_function([=]() { vmaDestroyAllocator(e->allocator); });
}


void init_pipelines(Engine* e)
{
    init_background_pipelines(e);
}
// ------------------------- Swapchain -------------------------

void init_swapchain(Engine* e, uint32_t width, uint32_t height) {
    vkb::SwapchainBuilder swapchainBuilder{ e->physicalDevice, e->device, e->surface };
    e->swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

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

    // CREATE PER-IMAGE SEMAPHORES
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

    // Initialize drawExtent for use in shader dispatch
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

    vmaCreateImage(e->allocator, &rimg_info, &rimg_allocinfo, &e->drawImage.image, &e->drawImage.allocation, nullptr);

    // Build a image-view for the draw image to use for rendering
    VkImageViewCreateInfo rview_info = imageview_create_info(e->drawImage.imageFormat, e->drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);

    VK_CHECK(vkCreateImageView(e->device, &rview_info, nullptr, &e->drawImage.imageView));

    // Add to deletion queues
    e->mainDeletionQueue.push_function([=]() {
        vkDestroyImageView(e->device, e->drawImage.imageView, nullptr);
        vmaDestroyImage(e->allocator, e->drawImage.image, e->drawImage.allocation);

        // DESTROY PER-IMAGE SEMAPHORES
        for (size_t i = 0; i < e->imageAvailableSemaphores.size(); i++) {
            vkDestroySemaphore(e->device, e->imageAvailableSemaphores[i], nullptr);
            vkDestroySemaphore(e->device, e->renderFinishedSemaphores[i], nullptr);
        }
    });
    std::printf("Swapchain initialized with %zu images\n", e->swapchainImages.size());
}

// ------------------------- Commands -------------------------

void init_commands(Engine* e) {
    VkCommandPoolCreateInfo commandPoolInfo{};
    commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolInfo.queueFamilyIndex = e->graphicsQueueFamily;

    for (int i = 0; i < static_cast<int>(FRAME_OVERLAP); i++) {
        VK_CHECK(vkCreateCommandPool(e->device, &commandPoolInfo, nullptr, &e->frames[i].commandPool));

        VkCommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAllocInfo.commandPool = e->frames[i].commandPool;
        cmdAllocInfo.commandBufferCount = 1;
        cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        VK_CHECK(vkAllocateCommandBuffers(e->device, &cmdAllocInfo, &e->frames[i].mainCommandBuffer));
    }

    std::printf("Commands initialized\n");
}

// ------------------------- Sync -------------------------

VkFenceCreateInfo fence_create_info(VkFenceCreateFlags flags) {
    VkFenceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    info.flags = flags;
    return info;
}

VkSemaphoreCreateInfo semaphore_create_info(VkSemaphoreCreateFlags flags /* = 0 */) {
    VkSemaphoreCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    info.flags = flags;
    return info;
}

void init_sync_structures(Engine* e) {
    VkFenceCreateInfo fenceCreateInfo = fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    VkSemaphoreCreateInfo semaphoreCreateInfo = semaphore_create_info(0);

    for (int i = 0; i < static_cast<int>(FRAME_OVERLAP); i++) {
        VK_CHECK(vkCreateFence(e->device, &fenceCreateInfo, nullptr, &e->frames[i].renderFence));
        VK_CHECK(vkCreateSemaphore(e->device, &semaphoreCreateInfo, nullptr, &e->frames[i].swapchainSemaphore));
        VK_CHECK(vkCreateSemaphore(e->device, &semaphoreCreateInfo, nullptr, &e->frames[i].renderSemaphore));
    }

    std::printf("Sync structures initialized\n");
}

// ----------------------------- Descriptors -------------------------------
void init_descriptors(Engine* e)
{
    std::vector<DescriptorAllocator::PoolSizeRatio> sizes =
    {
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

    e->mainDeletionQueue.push_function([&]() {
        e->globalDescriptorAllocator.destroy_pool(e->device);
        vkDestroyDescriptorSetLayout(e->device, e->drawImageDescriptorLayout, nullptr);
    });
}

//---------------------------------Pipeline-----------------------------
void init_background_pipelines(Engine* e)
{
    VkShaderModule computeDrawShader;
    if(!e->util.load_shader_module("shaders/gradient.spv", e->device, &computeDrawShader))
    {
        std::cout << "Shader load failed! Check the path or compilation." << std::endl;
        return;
    }

    // DEFINE THE PUSH CONSTANT RANGE
    VkPushConstantRange push_constant_range = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(PushConstants)
    };

    // CREATE THE PIPELINE LAYOUT WITH PUSH CONSTANTS
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = 1,
        .pSetLayouts = &e->drawImageDescriptorLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range
    };

    VK_CHECK(vkCreatePipelineLayout(e->device, &pipelineLayoutInfo, nullptr, &e->gradientPipelineLayout));

    // SETUP SHADER STAGE INFO
    VkPipelineShaderStageCreateInfo stageinfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = computeDrawShader,
        .pName = "main",
        .pSpecializationInfo = nullptr
    };

    // CREATE COMPUTE PIPELINE
    VkComputePipelineCreateInfo computePipelineCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = stageinfo,
        .layout = e->gradientPipelineLayout,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1
    };

    VK_CHECK(vkCreateComputePipelines(e->device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &e->gradientPipeline));

    vkDestroyShaderModule(e->device, computeDrawShader, nullptr);

    e->mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(e->device, e->gradientPipelineLayout, nullptr);
        vkDestroyPipeline(e->device, e->gradientPipeline, nullptr);
    });
}
// ------------------------- Helpers -------------------------

VkCommandBufferBeginInfo command_buffer_info(VkCommandBufferUsageFlags flags /* = 0 */) {
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

// ------------------------- Draw Functions -------------------------

void draw_background(VkCommandBuffer cmd, Engine* e)
{
    // Bind the compute pipeline
    // --- FIX 2: Corrected typo from POUTE to POINT ---
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, e->gradientPipeline);

    // Bind the descriptor set
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            e->gradientPipelineLayout, 0, 1,
                            &e->drawImageDescriptors, 0, nullptr);

    // --- FIX 3: Initialize .resolution as a vec2, not separate x/y members ---
    // This assumes your PushConstants struct in engine.hpp has a `glm::vec2 resolution` member
    PushConstants constants {
        .resolution = { (float)e->drawExtent.width, (float)e->drawExtent.height },
        .time = static_cast<float>(e->frameNumber) / 60.0f,
        .pulse = std::sin(e->frameNumber / 30.0f)
    };

    vkCmdPushConstants(cmd, e->gradientPipelineLayout,
                     VK_SHADER_STAGE_COMPUTE_BIT,
                     0, sizeof(PushConstants), &constants);

    // Dispatch the compute shader
    uint32_t dispatchX = (e->drawImage.imageExtent.width + 15) / 16;
    uint32_t dispatchY = (e->drawImage.imageExtent.height + 15) / 16;
    vkCmdDispatch(cmd, dispatchX, dispatchY, 1);
}


void engine_draw_frame(Engine* e) {
    FrameData& frame = get_current_frame();

    // Wait & reset fence
    VK_CHECK(vkWaitForFences(e->device, 1, &frame.renderFence, VK_TRUE, UINT64_MAX));
    frame.deletionQueue.flush();
    VK_CHECK(vkResetFences(e->device, 1, &frame.renderFence));

    // Acquire next swapchain image
    uint32_t swapchainImageIndex;
    VK_CHECK(vkAcquireNextImageKHR(
        e->device,
        e->swapchain,
        UINT64_MAX,
        e->imageAvailableSemaphores[0],  // Use per-image semaphore
        VK_NULL_HANDLE,
        &swapchainImageIndex
    ));

    VkCommandBuffer cmd = frame.mainCommandBuffer;
    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    VkCommandBufferBeginInfo cmdBeginInfo = command_buffer_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    // Draw operations
    transition_image(cmd, e->drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    draw_background(cmd, e);
    transition_image(cmd, e->drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    transition_image(cmd, e->swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // --- FIX 4: Changed source extent from VkExtent2D to VkExtent3D ---
    // The copy_image_to_image function expects the source extent to have a depth component.
    copy_image_to_image(cmd, e->drawImage.image, e->swapchainImages[swapchainImageIndex],
                        VkExtent3D{e->drawImage.imageExtent.width, e->drawImage.imageExtent.height, 1},
                        e->swapchainExtent);

    transition_image(cmd, e->swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    VK_CHECK(vkEndCommandBuffer(cmd));

    // Submit
    VkCommandBufferSubmitInfo cmdInfo = command_buffer_submit_info(cmd);

    VkSemaphoreSubmitInfo waitInfo = semaphore_submit_info(
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        e->imageAvailableSemaphores[0]  // Wait on acquire semaphore
    );

    VkSemaphoreSubmitInfo signalInfo = semaphore_submit_info(
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        e->renderFinishedSemaphores[0]  // Signal render semaphore
    );

    VkSubmitInfo2 submit = submit_info(&cmdInfo, &signalInfo, &waitInfo);
    VK_CHECK(vkQueueSubmit2(e->graphicsQueue, 1, &submit, frame.renderFence));

    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pWaitSemaphores = &e->renderFinishedSemaphores[0];  // Wait on render completion
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pSwapchains = &e->swapchain;
    presentInfo.swapchainCount = 1;
    presentInfo.pImageIndices = &swapchainImageIndex;

    VK_CHECK(vkQueuePresentKHR(e->graphicsQueue, &presentInfo));

    // Advance frame
    e->frameNumber++;
}

// ------------------------- Cleanup -------------------------
void engine_cleanup(Engine* e) {
    if (!e) return;

    if (e->device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(e->device);

    // 1. Per-frame cleanup
    for (int i = 0; i < FRAME_OVERLAP; i++) {
        e->frames[i].deletionQueue.flush();
        if (e->frames[i].renderFence) vkDestroyFence(e->device, e->frames[i].renderFence, nullptr);
        if (e->frames[i].renderSemaphore) vkDestroySemaphore(e->device, e->frames[i].renderSemaphore, nullptr);
        if (e->frames[i].swapchainSemaphore) vkDestroySemaphore(e->device, e->frames[i].swapchainSemaphore, nullptr);
        if (e->frames[i].commandPool) vkDestroyCommandPool(e->device, e->frames[i].commandPool, nullptr);
    }

    // 2. Main deletion queue (pipelines, descriptors, etc.)
    e->mainDeletionQueue.flush();

    // 3. Swapchain image views
    for (auto view : e->swapchainImageViews)
        if (view) vkDestroyImageView(e->device, view, nullptr);

    // 4. Swapchain
    if (e->swapchain) vkDestroySwapchainKHR(e->device, e->swapchain, nullptr);

    // 5. Device
    if (e->device != VK_NULL_HANDLE) {
        vkDestroyDevice(e->device, nullptr);
        e->device = VK_NULL_HANDLE;
    }

    // 6. Surface
    if (e->surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(e->instance, e->surface, nullptr);
        e->surface = VK_NULL_HANDLE;
    }

    // 7. Debug messenger
    if (e->debug_messenger != VK_NULL_HANDLE) {
        vkb::destroy_debug_utils_messenger(e->instance, e->debug_messenger);
        e->debug_messenger = VK_NULL_HANDLE;
    }

    // 8. Instance
    if (e->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(e->instance, nullptr);
        e->instance = VK_NULL_HANDLE;
    }

    // 9. Window
    if (e->window) {
        glfwDestroyWindow(e->window);
        e->window = nullptr;
    }
    glfwTerminate();

    std::printf("Engine cleaned up.\n");
}
