#include "engine.h"
#include "VkBootstrap.h"
#include <cstdio>
#include <cstdlib>

// vulkan_beta.h is needed for the Portability Subset extension name
#include <vulkan/vulkan_beta.h> 

template<typename T>
T get_or_abort(vkb::Result<T> result, const char* step) {
    if (!result) {
        std::printf("Vulkan Error in %s: %s\n", step, result.error().message().c_str());
        std::exit(1);
    }
    return result.value();
}

void init_vulkan(Engine* e) {
    // ── 1. Instance ───────────────────────────────────────────────────────────
    vkb::InstanceBuilder builder;
    builder.set_app_name("Synchrona")
        .request_validation_layers(true)
        .use_default_debug_messenger()
        .require_api_version(1, 3, 0)
        .enable_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

#if defined(__APPLE__)
    builder.enable_extension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    builder.set_instance_create_flags(VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR);
#endif

    auto inst_ret = builder.build();
    vkb::Instance vkb_inst = get_or_abort(inst_ret, "Instance creation");
    e->instance = vkb_inst.instance;
    e->debug_messenger = vkb_inst.debug_messenger;

    // ── 2. Window & Surface ───────────────────────────────────────────────────
    if (!glfwInit() || !glfwVulkanSupported()) {
        std::printf("GLFW Vulkan init failed\n");
        std::exit(1);
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    e->window = glfwCreateWindow(e->width, e->height, "Synchrona Engine", nullptr, nullptr);

    if (glfwCreateWindowSurface(e->instance, e->window, nullptr, &e->surface) != VK_SUCCESS) {
        std::printf("Failed to create Vulkan surface\n");
        std::exit(1);
    }

    // ── 3. Physical Device Selection ──────────────────────────────────────────
    // Request basic anisotropy and 64-bit ints for RT logic
    VkPhysicalDeviceFeatures coreFeatures{};
    coreFeatures.samplerAnisotropy = VK_TRUE;
    coreFeatures.shaderInt64 = VK_TRUE;

    vkb::PhysicalDeviceSelector selector{ vkb_inst, e->surface };
    selector.set_minimum_version(1, 3)
        .set_required_features(coreFeatures)
        .add_required_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)
        // Ray Tracing Extensions
        .add_required_extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
        .add_required_extension(VK_KHR_RAY_QUERY_EXTENSION_NAME)
        .add_required_extension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);

#if defined(__APPLE__)
    selector.add_required_extension(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

    auto phys_ret = selector.select();
    vkb::PhysicalDevice physicalDevice = get_or_abort(phys_ret, "Physical device selection");
    e->physicalDevice = physicalDevice.physical_device;

    // ── 4. Logical Device & Feature Chaining ──────────────────────────────────

    // 4a. Ray Query
    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{};
    rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    rayQueryFeatures.rayQuery = VK_TRUE;

    // 4b. Acceleration Structure
    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{};
    asFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    asFeatures.accelerationStructure = VK_TRUE;
    asFeatures.pNext = &rayQueryFeatures;

    // 4c. Vulkan 1.2 Features (Consolidated Bindless + Scalar Layout)
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = VK_TRUE;
    features12.scalarBlockLayout = VK_TRUE; // Fixed: Moved from standalone struct
    features12.runtimeDescriptorArray = VK_TRUE;
    features12.descriptorIndexing = VK_TRUE;
    features12.bufferDeviceAddressCaptureReplay = VK_TRUE;

    // Fixed: Enabling all Bindless bits required for Sponza's texture arrays
    features12.descriptorBindingPartiallyBound = VK_TRUE;
    features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    features12.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
    features12.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
    features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    features12.pNext = &asFeatures;

    // 4d. Vulkan 1.3 Features
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    features13.shaderDemoteToHelperInvocation = VK_TRUE;
    features13.pNext = &features12;

    // Build the device with the head of the chain (features13)
    vkb::DeviceBuilder deviceBuilder{ physicalDevice };
    auto dev_ret = deviceBuilder.add_pNext(&features13).build();
    vkb::Device vkbDevice = get_or_abort(dev_ret, "Logical device creation");

    e->device = vkbDevice.device;
    e->graphicsQueue = get_or_abort(vkbDevice.get_queue(vkb::QueueType::graphics), "Graphics queue");
    e->graphicsQueueFamily = get_or_abort(vkbDevice.get_queue_index(vkb::QueueType::graphics), "Graphics queue index");

    // ── 5. VMA Allocator ──────────────────────────────────────────────────────
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = e->physicalDevice;
    allocatorInfo.device = e->device;
    allocatorInfo.instance = e->instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    if (vmaCreateAllocator(&allocatorInfo, &e->allocator) != VK_SUCCESS) {
        std::printf("Failed to create VMA allocator\n");
        std::exit(1);
    }

    std::printf("Vulkan initialized successfully on %s\n", physicalDevice.properties.deviceName);
}