#include "engine.h"
#include "VkBootstrap.h"
#include <cstdlib>
#include <iostream>

// Define the global engine pointer
extern Engine* engine;
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
         .pNext = nullptr,
         .descriptorIndexing = VK_TRUE,
         .shaderInputAttachmentArrayDynamicIndexing = VK_FALSE,
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
void destroy_buffer(const AllocatedBuffer& buffer, VmaAllocator allocator) {
    vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
}