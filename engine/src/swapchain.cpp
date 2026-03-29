#include "engine.h"
#include "VkBootstrap.h"
#include <algorithm>
#include <cstdio>

// ─── Destroy ─────────────────────────────────────────────────────────────────
void destroy_swapchain(Engine* e) {
    if (e->swapchain == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(e->device);

    for (auto view : e->swapchainImageViews)
        vkDestroyImageView(e->device, view, nullptr);
    e->swapchainImageViews.clear();
    e->swapchainImages.clear();

    for (size_t i = 0; i < e->imageAvailableSemaphores.size(); i++) {
        vkDestroySemaphore(e->device, e->imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(e->device, e->renderFinishedSemaphores[i], nullptr);
    }
    e->imageAvailableSemaphores.clear();
    e->renderFinishedSemaphores.clear();

    vkDestroySwapchainKHR(e->device, e->swapchain, nullptr);
    e->swapchain = VK_NULL_HANDLE;
}

// ─── Internal builder (shared by init and resize) ────────────────────────────
static void build_swapchain(Engine* e, uint32_t width, uint32_t height) {
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(e->physicalDevice, e->surface, &caps));

    if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0)
        return; // minimized — caller should defer

    width  = std::clamp(width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);

    auto vkbSwapchain = vkb::SwapchainBuilder{ e->physicalDevice, e->device, e->surface }
        .set_desired_format(VkSurfaceFormatKHR{
            .format     = VK_FORMAT_B8G8R8A8_UNORM,
            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(width, height)
        .add_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                               VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build();

    if (!vkbSwapchain) {
        std::printf("❌ Failed to build swapchain\n");
        std::exit(1);
    }

    e->swapchain            = vkbSwapchain->swapchain;
    e->swapchainImages      = vkbSwapchain->get_images().value();
    e->swapchainImageViews  = vkbSwapchain->get_image_views().value();
    e->swapchainImageFormat = vkbSwapchain->image_format;
    e->swapchainExtent      = vkbSwapchain->extent; // authoritative pixel size on ALL platforms

    VkSemaphoreCreateInfo semInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    e->imageAvailableSemaphores.resize(e->swapchainImages.size());
    e->renderFinishedSemaphores.resize(e->swapchainImages.size());
    for (size_t i = 0; i < e->swapchainImages.size(); i++) {
        VK_CHECK(vkCreateSemaphore(e->device, &semInfo, nullptr, &e->imageAvailableSemaphores[i]));
        VK_CHECK(vkCreateSemaphore(e->device, &semInfo, nullptr, &e->renderFinishedSemaphores[i]));
    }

    e->memoryStats.swapchainMemoryBytes = 0;
    for (auto& img : e->swapchainImages) {
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(e->device, img, &req);
        e->memoryStats.swapchainMemoryBytes += req.size;
    }
    e->memoryStats.totalMemoryBytes = e->memoryStats.swapchainMemoryBytes
                                    + e->memoryStats.imageMemoryBytes
                                    + e->memoryStats.bufferMemoryBytes;

    std::printf("✅ Swapchain %ux%u (%zu images)\n",
        e->swapchainExtent.width, e->swapchainExtent.height, e->swapchainImages.size());
}

// ─── Init (first time) ───────────────────────────────────────────────────────
// FIX: init_swapchain ONLY builds the swapchain and updates e->swapchainExtent.
//      create_draw_image and init_depth_image are called separately in engine.cpp
//      using e->swapchainExtent — which is the correct pixel size on all platforms.
//      Previously this function also created the draw image inline, which was then
//      immediately destroyed and recreated by engine.cpp with wrong (window) dimensions.
void init_swapchain(Engine* e, uint32_t width, uint32_t height) {
    build_swapchain(e, width, height);

    e->mainDeletionQueue.push_function([=]() {
        destroy_swapchain(e);
    });
}

// ─── Resize ──────────────────────────────────────────────────────────────────
void resize_swapchain(Engine* e) {
    if (!e->resize_requested) return;
    e->resize_requested = false;

    vkDeviceWaitIdle(e->device);

    // Capture old extent before destroying (used as hint for build_swapchain)
    uint32_t oldW = e->swapchainExtent.width;
    uint32_t oldH = e->swapchainExtent.height;

    destroy_swapchain(e);
    destroy_draw_image(e);
    if (e->depthImage.image != VK_NULL_HANDLE) {
        destroy_image(e->depthImage, e);
        e->depthImage = {};
    }

    // build_swapchain queries surface caps and sets e->swapchainExtent to true pixel size
    build_swapchain(e, oldW, oldH);

    // Use the updated swapchainExtent — correct on Mac Retina and Windows alike
    create_draw_image(e, e->swapchainExtent.width, e->swapchainExtent.height);
    init_depth_image(e, e->swapchainExtent.width, e->swapchainExtent.height);
}
