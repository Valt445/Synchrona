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
#include "debug_ui.h"
#include "images.h"
#include "helper.h"
#include "descriptors.h"
#include <iostream>
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"
#include "graphics_pipeline.h"


// Define the global engine pointer declared as `extern` in the header
Engine* engine = nullptr;

// ------------------------- Initialization -------------------------

// Initialize the Engine
void init(Engine* e, uint32_t x, uint32_t y) {
    *e = Engine{};
    ::engine = e;

    init_vulkan(e);
    init_swapchain(e, x, y);
    init_commands(e);
    init_sync_structures(e);
    init_descriptors(e);
    init_pipelines(e);
    init_triangle_pipeline(e);
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
    e->window = glfwCreateWindow(1720, 1200, "Vulkan Window", nullptr, nullptr);
    if (!e->window) {
        std::printf("Failed to create GLFW window\n");
        std::exit(1);
    }

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
    // create descriptor pool (same pool sizes you already use)
    VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    // store pool on engine so we can shutdown in explicit order
    VK_CHECK(vkCreateDescriptorPool(e->device, &pool_info, nullptr, &e->imguiDescriptorPool));

    // ImGui core init
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForVulkan(e->window, true);

    // Build a proper PipelineRenderingCreateInfo local object
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

    // Use actual swapchain image count
    uint32_t scImageCount = static_cast<uint32_t>(e->swapchainImages.size());
    if (scImageCount == 0) scImageCount = 2;
    init_info.MinImageCount = scImageCount;
    init_info.ImageCount = scImageCount;
    init_info.UseDynamicRendering = VK_TRUE;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    // attach pipeline rendering info
    init_info.PipelineRenderingCreateInfo = pipelineRenderingInfo;

    ImGui_ImplVulkan_Init(&init_info);

    debug_ui_init(e);
    // now add an explicit cleanup action (we will call it in engine_cleanup before device destruction)
    e->mainDeletionQueue.push_function([=]() {
        // Ensure ImGui is shutdown before the descriptor pool is destroyed
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        vkDestroyDescriptorPool(e->device, e->imguiDescriptorPool, nullptr);
    });
}

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

    // Initialize memory stats
    e->memoryStats = {};

    // Track swapchain memory - ONLY if images are valid
    for (auto& image : e->swapchainImages) {
        if (image != VK_NULL_HANDLE) {
            VkMemoryRequirements memReq;
            vkGetImageMemoryRequirements(e->device, image, &memReq);
            e->memoryStats.swapchainMemoryBytes += memReq.size;
            e->memoryStats.totalMemoryBytes += memReq.size;
        }
    }

    // 🛡️ DIVINE FIX: PER-IMAGE SEMAPHORES
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

    // CREATE THE IMAGE FIRST
    vmaCreateImage(e->allocator, &rimg_info, &rimg_allocinfo, &e->drawImage.image, &e->drawImage.allocation, nullptr);

    // NOW get memory requirements (AFTER image creation)
    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(e->device, e->drawImage.image, &memReq);
    e->memoryStats.imageMemoryBytes += memReq.size;
    e->memoryStats.totalMemoryBytes += memReq.size;

    VkImageViewCreateInfo rview_info = imageview_create_info(e->drawImage.imageFormat, e->drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);

    VK_CHECK(vkCreateImageView(e->device, &rview_info, nullptr, &e->drawImage.imageView));

    e->mainDeletionQueue.push_function([=]() {
        vkDestroyImageView(e->device, e->drawImage.imageView, nullptr);
        vmaDestroyImage(e->allocator, e->drawImage.image, e->drawImage.allocation);
        
        // 🛡️ DIVINE FIX: CLEANUP PER-IMAGE SEMAPHORES
        for (size_t i = 0; i < e->imageAvailableSemaphores.size(); i++) {
            vkDestroySemaphore(e->device, e->imageAvailableSemaphores[i], nullptr);
            vkDestroySemaphore(e->device, e->renderFinishedSemaphores[i], nullptr);
        }
    });
    std::printf("Swapchain initialized with %zu images\n", e->swapchainImages.size());
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
    if (clear)
    {
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

    for (int i = 0; i < static_cast<int>(FRAME_OVERLAP); i++) {
        VK_CHECK(vkCreateCommandPool(e->device, &commandPoolInfo, nullptr, &e->frames[i].commandPool));

        VkCommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAllocInfo.commandPool = e->frames[i].commandPool;
        cmdAllocInfo.commandBufferCount = 1;
        cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        VK_CHECK(vkAllocateCommandBuffers(e->device, &cmdAllocInfo, &e->frames[i].mainCommandBuffer));
    }

    VK_CHECK(vkCreateCommandPool(e->device, &commandPoolInfo, nullptr, &e->immCommandPool));

    // allocating command buffer for imidiate submition without synchronization mainly for imgui
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

    for (int i = 0; i < static_cast<int>(FRAME_OVERLAP); i++) {
        VK_CHECK(vkCreateFence(e->device, &fenceCreateInfo, nullptr, &e->frames[i].renderFence));
        VK_CHECK(vkCreateSemaphore(e->device, &semaphoreCreateInfo, nullptr, &e->frames[i].swapchainSemaphore));
        VK_CHECK(vkCreateSemaphore(e->device, &semaphoreCreateInfo, nullptr, &e->frames[i].renderSemaphore));
        ComputeEffect& selected = engine->backgroundEffects[engine->currentBackgroundEffect];
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
    
    // 🛡️ CHECK ENGINE STATE
    if (e == nullptr) {
        std::cout << "❌ ENGINE IS NULL!" << std::endl;
        std::exit(1);
    }
    if (e->device == VK_NULL_HANDLE) {
        std::cout << "❌ DEVICE IS NULL!" << std::endl;
        std::exit(1);
    }
    std::cout << "✅ Engine and device are valid" << std::endl;

    // 🛡️ SHADER LOADING
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

    // 🛡️ PIPELINE LAYOUT
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

    // 🛡️ CHECK DESCRIPTOR LAYOUT
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

    // 🛡️ PIPELINE CREATION
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

    // Create gradient pipeline
    VkResult pipelineResult = vkCreateComputePipelines(e->device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &e->gradientPipeline);
    if (pipelineResult != VK_SUCCESS) {
        std::cout << "❌ GRADIENT PIPELINE CREATION FAILED: " << pipelineResult << std::endl;
        std::exit(1);
    }

    // Create sky pipeline
    computePipelineCreateInfo.stage.module = skyShader;
    VkPipeline skyPipeline;
    VkResult skyPipelineResult = vkCreateComputePipelines(e->device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &skyPipeline);
    if (skyPipelineResult != VK_SUCCESS) {
        std::cout << "❌ SKY PIPELINE CREATION FAILED: " << skyPipelineResult << std::endl;
        std::exit(1);
    }

    // Create and populate background effects
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

    // Add the 2 background effects into the array
    e->backgroundEffects.push_back(gradient);
    e->backgroundEffects.push_back(sky);

    std::cout << "✅ Compute pipelines created!" << std::endl;

    // 🛡️ CLEANUP - Add pipelines to deletion queue
    e->mainDeletionQueue.push_function([=]() {
        std::cout << "Cleaning up pipelines..." << std::endl;
        
        // Destroy all pipelines in background effects
        for (auto& effect : e->backgroundEffects) {
            if (effect.pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(e->device, effect.pipeline, nullptr);
                effect.pipeline = VK_NULL_HANDLE; // Mark as destroyed
            }
        }
        
        // Destroy pipeline layout
        if (e->gradientPipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(e->device, e->gradientPipelineLayout, nullptr);
            e->gradientPipelineLayout = VK_NULL_HANDLE; // Mark as destroyed
        }
        
        // Clear the effects vector
        e->backgroundEffects.clear();
        
        std::cout << "Pipeline cleanup complete!" << std::endl;
    });

    // 🛡️ Clean up shader modules immediately (they're no longer needed after pipeline creation)
    vkDestroyShaderModule(e->device, computeDrawShader, nullptr);
    vkDestroyShaderModule(e->device, skyShader, nullptr);
    
    std::cout << "✅ Pipeline initialization complete!" << std::endl;
}

// Graphics Pipeline
void init_triangle_pipeline(Engine* e)
{
    VkShaderModule triangleFragShader;
    if (!e->util.load_shader_module("shaders/triangle_frag.frag.spv", e->device, &triangleFragShader)) {
        std::cout << "Failed to load triangle fragment shader\n";
        std::exit(1);
    }

    VkShaderModule triangleVertShader;
    if (!e->util.load_shader_module("shaders/triangle_vert.vert.spv", e->device, &triangleVertShader)) {
        std::cout << "Failed to load triangle vertex shader\n";
        std::exit(1);
    }

    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipeline_layout_info = e->util.pipeline_layout_create_info();
    if (vkCreatePipelineLayout(e->device, &pipeline_layout_info, nullptr, &e->trianglePipelineLayout) != VK_SUCCESS) {
        std::cerr << "Failed to create triangle pipeline layout\n";
        std::exit(1);
    }

    // Use a LOCAL pipeline builder instead of the shared one
    PipelineBuilder pbuilder = {}; // Create a fresh builder
    pbuilder.pipelineLayout = e->trianglePipelineLayout;

    // Set shaders & states
    set_shaders(triangleVertShader, triangleFragShader, pbuilder);
    set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, pbuilder);
    set_polygon_mode(VK_POLYGON_MODE_FILL, pbuilder);
    set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE, pbuilder);
    set_multisampling_none(pbuilder);
    disable_blending(pbuilder);
    disable_depthtest(pbuilder);

    set_color_attachment_format(e->drawImage.imageFormat, pbuilder);
    set_depth_format(VK_FORMAT_UNDEFINED, pbuilder);

    // Build pipeline
    e->trianglePipeline = build_pipeline(e->device, pbuilder);
    if (e->trianglePipeline == VK_NULL_HANDLE) {
        std::cerr << "❌ Failed to create triangle pipeline. Aborting." << std::endl;
        std::exit(1);
    }

    // Clean up shader modules
    vkDestroyShaderModule(e->device, triangleVertShader, nullptr);
    vkDestroyShaderModule(e->device, triangleFragShader, nullptr);

    e->mainDeletionQueue.push_function([=]() {
        vkDestroyPipelineLayout(e->device, e->trianglePipelineLayout, nullptr);
        vkDestroyPipeline(e->device, e->trianglePipeline, nullptr);
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

// ------------------------- Draw Functions -------------------------

void draw_geometry(Engine* e, VkCommandBuffer cmd)
{
    VkRenderingAttachmentInfo colorAttachment = attachment_info(e->drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkRenderingInfo renderInfo = e->util.rendering_info(e->drawExtent, &colorAttachment, nullptr);
	  vkCmdBeginRendering(cmd, &renderInfo);

	  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e->trianglePipeline);

	//set dynamic viewport and scissor
	  VkViewport viewport = {};
	  viewport.x = 0;
	  viewport.y = 0;
	  viewport.width = e->drawExtent.width;
	  viewport.height = e->drawExtent.height;
	  viewport.minDepth = 0.f;
	  viewport.maxDepth = 1.f;

	  vkCmdSetViewport(cmd, 0, 1, &viewport);

	  VkRect2D scissor = {};
	  scissor.offset.x = 0;
	  scissor.offset.y = 0;
	  scissor.extent.width = e->drawExtent.width;
	  scissor.extent.height = e->drawExtent.height;

	  vkCmdSetScissor(cmd, 0, 1, &scissor);

	//launch a draw command to draw 3 vertices
	  vkCmdDraw(cmd, 3, 1, 0, 0);

	  vkCmdEndRendering(cmd);
}


void draw_background(VkCommandBuffer cmd, Engine* e) {

    ComputeEffect& effect = e->backgroundEffects[e->currentBackgroundEffect];
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            e->gradientPipelineLayout, 0, 1,
                            &e->drawImageDescriptors, 0, nullptr);

    // 🛡️ DIVINE FIX: PROPER PUSH CONSTANTS

    PushConstants constants;
    constants.data1 = glm::vec4(0, 1, 0, 1);
    constants.data2 = glm::vec4(0, 0, 1, 0);
    
    vkCmdPushConstants(cmd, e->gradientPipelineLayout,
                     VK_SHADER_STAGE_COMPUTE_BIT,
                     0, sizeof(PushConstants), &effect.data);

    uint32_t dispatchX = (e->drawImage.imageExtent.width + 15) / 16;
    uint32_t dispatchY = (e->drawImage.imageExtent.height + 15) / 16;
    vkCmdDispatch(cmd, dispatchX, dispatchY, 1);
}

void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView, Engine* e)
{
    // Start new ImGui frame
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    
    // Render debug UI
    debug_ui_render();
    
    // End ImGui frame and render
    ImGui::Render();

    VkRenderingAttachmentInfo colorAttachment = attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderInfo = e->util.rendering_info(e->swapchainExtent, &colorAttachment, nullptr);

    vkCmdBeginRendering(cmd, &renderInfo);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRendering(cmd);
}

 void engine_draw_frame(Engine* e) {
    FrameData& frame = get_current_frame();

    VK_CHECK(vkWaitForFences(e->device, 1, &frame.renderFence, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(e->device, 1, &frame.renderFence));

    uint32_t swapchainImageIndex;
    VkResult acquireResult = vkAcquireNextImageKHR(
        e->device,
        e->swapchain,
        UINT64_MAX,
        frame.swapchainSemaphore,
        VK_NULL_HANDLE,
        &swapchainImageIndex
    );

    if (acquireResult != VK_SUCCESS) {
        std::printf("Image acquisition failed: %d\n", acquireResult);
        return;
    }

    // Update debug UI timing
    static auto lastTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
    lastTime = currentTime;
    
    debug_ui_update(deltaTime);

    VkCommandBuffer cmd = frame.mainCommandBuffer;
    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    VkCommandBufferBeginInfo cmdBeginInfo = command_buffer_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
    // Transitioning imageformats for graphics pipeline
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
    
    // Proper layout transitions:
    // Start with undefined -> general for compute
    transition_image(cmd, e->drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    
    // Draw background (compute)
    draw_background(cmd, e);
    
    // General -> color attachment for graphics
    transition_image(cmd, e->drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    
    // Draw geometry
    draw_geometry(e, cmd);    
    
    // Color attachment -> transfer src for copy
    transition_image(cmd, e->drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    
    copy_image_to_image(cmd, e->drawImage.image, e->swapchainImages[swapchainImageIndex],
                        VkExtent3D{e->drawImage.imageExtent.width, e->drawImage.imageExtent.height, 1},
                        e->swapchainExtent);

    // Transition swapchain image to COLOR_ATTACHMENT_OPTIMAL for ImGui rendering
    transition_image(cmd, e->swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // Render ImGui
    draw_imgui(cmd, e->swapchainImageViews[swapchainImageIndex], e);

    // Transition swapchain image back to PRESENT_SRC_KHR for presentation
    transition_image(cmd, e->swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdInfo = command_buffer_submit_info(cmd);
    VkSemaphoreSubmitInfo waitInfo = semaphore_submit_info(
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        frame.swapchainSemaphore
    );
    VkSemaphoreSubmitInfo signalInfo = semaphore_submit_info(
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        e->renderFinishedSemaphores[swapchainImageIndex]
    );

    VkSubmitInfo2 submit = submit_info(&cmdInfo, &signalInfo, &waitInfo);
    VK_CHECK(vkQueueSubmit2(e->graphicsQueue, 1, &submit, frame.renderFence));

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pWaitSemaphores = &e->renderFinishedSemaphores[swapchainImageIndex];
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pSwapchains = &e->swapchain;
    presentInfo.swapchainCount = 1;
    presentInfo.pImageIndices = &swapchainImageIndex;

    VK_CHECK(vkQueuePresentKHR(e->graphicsQueue, &presentInfo));

    e->frameNumber++;
}

// ------------------------- Cleanup -------------------------
// -------------------- IMGUI INITIALIZATION --------------------
void engine_cleanup(Engine* e) {
    if (!e) return;

    // Wait for GPU to finish work
    vkDeviceWaitIdle(e->device);

    // 1. Destroy per-frame resources
    for (int i = 0; i < FRAME_OVERLAP; i++) {
        // Flush deletion queue for this frame first (to clean descriptor pools, buffers, etc.)
        e->frames[i].deletionQueue.flush();

        vkDestroyCommandPool(e->device, e->frames[i].commandPool, nullptr);
        vkDestroySemaphore(e->device, e->frames[i].swapchainSemaphore, nullptr);
        vkDestroySemaphore(e->device, e->frames[i].renderSemaphore, nullptr);
        vkDestroyFence(e->device, e->frames[i].renderFence, nullptr);
    }

    // 2. Flush main deletion queue (ImGui, pipelines, allocators, etc.)
    e->mainDeletionQueue.flush();

    // 3. Destroy swapchain image views
    ComputeEffect& selected = engine->backgroundEffects[engine->currentBackgroundEffect];
    for (auto view : e->swapchainImageViews) {
        vkDestroyImageView(e->device, view, nullptr);
    }

    // 4. Destroy swapchain
    if (e->swapchain) vkDestroySwapchainKHR(e->device, e->swapchain, nullptr);

    // 5. Destroy device
    if (e->device) vkDestroyDevice(e->device, nullptr);

    // 6. Destroy debug messenger **before destroying the instance**
    if (e->debug_messenger) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(e->instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func) {
            func(e->instance, e->debug_messenger, nullptr);
        }
        e->debug_messenger = VK_NULL_HANDLE;
    }

    // 7. Destroy surface
    if (e->surface) vkDestroySurfaceKHR(e->instance, e->surface, nullptr);

    // 8. Destroy Vulkan instance
    if (e->instance) vkDestroyInstance(e->instance, nullptr);

    // 9. Destroy window
    if (e->window) glfwDestroyWindow(e->window);
    
    // 10. Terminate GLFW
    glfwTerminate();

    std::cout << "Engine cleanup complete!" << std::endl;
}
