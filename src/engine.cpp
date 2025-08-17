#include "engine.hpp"
#include <cstdint>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>
#include "VkBootstrap.h"

// Initialize the Engine struct to zero / defaults
void init(Engine* engine, uint32_t x, uint32_t y) {
    memset(engine, 0, sizeof(*engine));
    init_vulkan(engine);
    init_swapchain(engine, x, y);
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

    //create device selector

    VkPhysicalDeviceVulkan13Features features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features.dynamicRendering = true;
    features.synchronization2 = true;

    

    VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing = true;

    vkb::PhysicalDeviceSelector selector{ vkb_inst };
    vkb::PhysicalDevice PhysicalDevice = selector
        .set_minimum_version(1, 3)
        .set_required_features_13(features)
        .set_required_features_12(features12)
        .set_surface(engine->surface)
        .select()
        .value();

    vkb::DeviceBuilder deviceBuilder{ PhysicalDevice };
    vkb::Device vkbDevice = deviceBuilder.build().value();

    engine->device = vkbDevice.device;
    engine->physicalDevice = PhysicalDevice.physical_device;

    engine->graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    engine->graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
    
}

FrameData get_current_frame(Engine* engine)
{
    return engine->frames[engine->frameNumber % FRAME_OVERLAP];
} 

// Placeholder: swapchain initialization
void init_swapchain(Engine* engine, uint32_t width, uint32_t height) {
    // TODO: Implement swapchain creation

    vkb::SwapchainBuilder swapchainBuilder{ engine->physicalDevice, engine->device, engine->surface };
    engine->swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

    vkb::Swapchain vkbSwapchain = swapchainBuilder
        .set_desired_format(VkSurfaceFormatKHR{ .format = engine->swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(width, height)
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build()
        .value();



    engine->swapchainExtent = vkbSwapchain.extent;
    engine->swapchain = vkbSwapchain.swapchain;
    engine->swapchainImages = vkbSwapchain.get_images().value();
    engine->swapchainImageViews = vkbSwapchain.get_image_views().value();
      
    printf("Swapchain initialized (placeholder)n");
}

// Placeholder: command buffers and pools
void init_commands(Engine* engine) {

    VkCommandPoolCreateInfo commandPoolInfo = {};
    commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolInfo.pNext = nullptr;
    commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolInfo.queueFamilyIndex = engine->graphicsQueueFamily;

    for (int i = 0; i < FRAME_OVERLAP; i++)
    {
		    VK_CHECK(vkCreateCommandPool(engine->device, &commandPoolInfo, nullptr, &engine->frames[i].commandPool));
		    VkCommandBufferAllocateInfo cmdAllocInfo = {};
		    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		    cmdAllocInfo.pNext = nullptr;
		    cmdAllocInfo.commandPool = engine->frames[i].commandPool;
		    cmdAllocInfo.commandBufferCount = 1;
		    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

		    VK_CHECK(vkAllocateCommandBuffers(engine->device, &cmdAllocInfo, &engine->frames[i].mainCommandBuffer));
    }
    
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
    vkDestroySwapchainKHR(engine->device, engine->swapchain, nullptr);

	// destroy swapchain resources

	  for (int i = 0; i < FRAME_OVERLAP; i++)
	  {
	      vkDestroyCommandPool(engine->device, engine->frames[i].commandPool, nullptr);
	  }
	  if (engine->swapchain != VK_NULL_HANDLE)
	  {
	      for (int i = 0; i < engine->swapchainImageViews.size(); i++) 
	      {
	        vkDestroyImageView(engine->device, engine->swapchainImageViews[i], nullptr);
	      }
	  }
    if (engine->surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(engine->instance, engine->surface, nullptr);
    }
    if (engine->instance != VK_NULL_HANDLE)
    {
         vkDestroyInstance(engine->instance, nullptr);
    }
    if(engine->debug_messenger != VK_NULL_HANDLE)
    {
        vkb::destroy_debug_utils_messenger(engine->instance, engine->debug_messenger);       
    }
        
    if (engine->window)
        glfwDestroyWindow(engine->window);
    glfwTerminate();

    printf("Engine cleaned up.\n");
}

