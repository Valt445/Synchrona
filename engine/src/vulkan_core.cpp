#include "engine.h"
#include "VkBootstrap.h"
#include <cstdio>
#include <cstdlib>
#include <vulkan/vulkan_beta.h>   // VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME

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
    auto inst_ret = builder
        .set_app_name("Malike")
        .request_validation_layers(true)
        .use_default_debug_messenger()
        .require_api_version(1, 3, 0)
        .enable_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)
        .enable_extension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)
        .build();
    vkb::Instance vkb_inst = get_or_abort(inst_ret, "Instance creation");
    e->instance        = vkb_inst.instance;
    e->debug_messenger = vkb_inst.debug_messenger;

    // ── 2. GLFW + Window + Surface ────────────────────────────────────────────
    if (!glfwInit() || !glfwVulkanSupported()) {
        std::printf("GLFW Vulkan init failed\n");
        std::exit(1);
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    e->window = glfwCreateWindow(e->width, e->height, "Malike", nullptr, nullptr);
    if (!e->window) {
        std::printf("Failed to create GLFW window\n");
        std::exit(1);
    }
    glfwSetWindowUserPointer(e->window, e);

    if (glfwCreateWindowSurface(e->instance, e->window, nullptr, &e->surface) != VK_SUCCESS) {
        std::printf("Failed to create Vulkan surface\n");
        std::exit(1);
    }

    // ── 3. Physical Device ────────────────────────────────────────────────────
    VkPhysicalDeviceFeatures coreFeatures{};
    coreFeatures.samplerAnisotropy = VK_TRUE;

    vkb::PhysicalDeviceSelector selector{ vkb_inst, e->surface };
    auto phys_ret = selector
        .set_minimum_version(1, 3)
        .set_required_features(coreFeatures)
        .set_surface(e->surface)
        .add_required_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)
        .add_required_extension(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)
        .select();
    vkb::PhysicalDevice physicalDevice = get_or_abort(phys_ret, "Physical device selection");

    std::printf("✅ Selected GPU: %s\n", physicalDevice.properties.deviceName);
    e->physicalDevice = physicalDevice.physical_device;

    // ── 4. Logical Device ─────────────────────────────────────────────────────

    // Vulkan 1.2 — bindless + buffer device address
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType                                             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress                               = VK_TRUE;  // required by VMA flag + BDA
    features12.runtimeDescriptorArray                            = VK_TRUE;  // unsized descriptor arrays in shaders
    features12.descriptorIndexing                                = VK_TRUE;  // master switch for descriptor indexing
    features12.descriptorBindingPartiallyBound                   = VK_TRUE;  // bindless: not all slots need to be filled
    features12.descriptorBindingSampledImageUpdateAfterBind      = VK_TRUE;  // update bindless textures after bind
    features12.descriptorBindingStorageImageUpdateAfterBind      = VK_TRUE;  // update storage images after bind
    features12.descriptorBindingUniformBufferUpdateAfterBind     = VK_TRUE;  // update UBOs after bind
    features12.shaderSampledImageArrayNonUniformIndexing         = VK_TRUE;  // non-uniform texture indexing in shaders

    // Vulkan 1.3 — dynamic rendering + sync2 + scalar layout
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType                       = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering            = VK_TRUE;  // renderpass-less rendering
    features13.synchronization2            = VK_TRUE;  // vkCmdPipelineBarrier2 / vkQueueSubmit2
    features13.shaderDemoteToHelperInvocation = VK_TRUE;  // discard → demote in mesh shader

    // Scalar block layout — fixes push constant straddling vector error
    VkPhysicalDeviceScalarBlockLayoutFeatures scalarLayout{};
    scalarLayout.sType             = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES;
    scalarLayout.scalarBlockLayout = VK_TRUE;

    vkb::DeviceBuilder deviceBuilder{ physicalDevice };
    auto dev_ret = deviceBuilder
        .add_pNext(&features12)
        .add_pNext(&features13)
        .add_pNext(&scalarLayout)
        .build();
    vkb::Device vkbDevice = get_or_abort(dev_ret, "Logical device creation");

    e->device             = vkbDevice.device;
    e->graphicsQueue      = get_or_abort(vkbDevice.get_queue(vkb::QueueType::graphics),       "Graphics queue");
    e->graphicsQueueFamily = get_or_abort(vkbDevice.get_queue_index(vkb::QueueType::graphics), "Graphics queue index");

    // ── 5. VMA ────────────────────────────────────────────────────────────────
    // VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT is now valid because
    // bufferDeviceAddress was enabled above. Without that feature enabled at
    // the device level this flag caused VUID-VkMemoryAllocateInfo-flags-03331.
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = e->physicalDevice;
    allocatorInfo.device         = e->device;
    allocatorInfo.instance       = e->instance;
    allocatorInfo.flags          = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &e->allocator));

    e->mainDeletionQueue.push_function([=]() {
        if (e->allocator) vmaDestroyAllocator(e->allocator);
    });

    std::printf("✅ Vulkan initialized successfully on %s (MoltenVK 1.4.3)\n",
                physicalDevice.properties.deviceName);
}
