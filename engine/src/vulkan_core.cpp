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

    VkPhysicalDeviceVulkan12Features features12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    features12.pNext = nullptr;
    features12.samplerMirrorClampToEdge = VK_FALSE;
    features12.drawIndirectCount = VK_FALSE;
    features12.storageBuffer8BitAccess = VK_FALSE;
    features12.uniformAndStorageBuffer8BitAccess = VK_FALSE;
    features12.storagePushConstant8 = VK_FALSE;
    features12.shaderBufferInt64Atomics = VK_FALSE;
    features12.shaderSharedInt64Atomics = VK_FALSE;
    features12.descriptorIndexing = VK_TRUE;
    features12.shaderInputAttachmentArrayDynamicIndexing = VK_FALSE;
    features12.shaderUniformTexelBufferArrayDynamicIndexing = VK_FALSE;
    features12.shaderStorageTexelBufferArrayDynamicIndexing = VK_FALSE;
    features12.shaderUniformBufferArrayNonUniformIndexing = VK_FALSE;
    features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    features12.shaderStorageBufferArrayNonUniformIndexing = VK_FALSE;
    features12.shaderStorageImageArrayNonUniformIndexing = VK_FALSE;
    features12.shaderInputAttachmentArrayNonUniformIndexing = VK_FALSE;
    features12.shaderUniformTexelBufferArrayNonUniformIndexing = VK_FALSE;
    features12.shaderStorageTexelBufferArrayNonUniformIndexing = VK_FALSE;
    features12.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
    features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    features12.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
    features12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
    features12.descriptorBindingUniformTexelBufferUpdateAfterBind = VK_FALSE;
    features12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
    features12.descriptorBindingPartiallyBound = VK_TRUE;
    features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
    features12.runtimeDescriptorArray = VK_TRUE;
    features12.samplerFilterMinmax = VK_FALSE;
    features12.scalarBlockLayout = VK_TRUE; // This is the one we added!
    features12.imagelessFramebuffer = VK_FALSE;
    features12.uniformBufferStandardLayout = VK_FALSE;
    features12.shaderSubgroupExtendedTypes = VK_FALSE;
    features12.separateDepthStencilLayouts = VK_FALSE;
    features12.hostQueryReset = VK_FALSE;
    features12.timelineSemaphore = VK_FALSE;
    features12.bufferDeviceAddress = VK_TRUE;
    features12.shaderOutputViewportIndex = VK_FALSE;
    features12.shaderOutputLayer = VK_FALSE;
    features12.vulkanMemoryModel = VK_FALSE;
    features12.vulkanMemoryModelDeviceScope = VK_FALSE;
    features12.vulkanMemoryModelAvailabilityVisibilityChains = VK_FALSE;
    features12.shaderOutputViewportIndex = VK_FALSE;
    features12.shaderOutputLayer = VK_FALSE;
    features12.subgroupBroadcastDynamicId = VK_FALSE;
    

    VkPhysicalDeviceVulkan13Features features13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = nullptr,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE
    };

    VkPhysicalDeviceFeatures coreFeatures{
        .samplerAnisotropy = VK_TRUE, 
        .shaderStorageImageMultisample = VK_TRUE,
        .shaderInt64 = VK_TRUE,
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
