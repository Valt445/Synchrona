#include "engine.h"
#include "VkBootstrap.h"
#include <algorithm>
#include <cstdio>

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
static void build_swapchain(Engine* e, uint32_t width, uint32_t height) {
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(e->physicalDevice, e->surface, &caps));

    if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0)
        return; 

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
    e->swapchainExtent      = vkbSwapchain->extent; 

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


void init_swapchain(Engine* e, uint32_t width, uint32_t height) {
    build_swapchain(e, width, height);

    e->mainDeletionQueue.push_function([=]() {
        destroy_swapchain(e);
    });
}


void resize_swapchain(Engine* e) {
    if (!e->resize_requested) return;
    e->resize_requested = false;

    vkDeviceWaitIdle(e->device);


    uint32_t oldW = e->swapchainExtent.width;
    uint32_t oldH = e->swapchainExtent.height;
    destroy_swapchain(e);
    build_swapchain(e, oldW, oldH);

    
    if (e->drawExtent.width != 3840 || e->drawExtent.height != 2160) {
        destroy_draw_image(e);
        destroy_depth_image(e);

        e->drawExtent.width = 3840;
        e->drawExtent.height = 2160;

        create_draw_image(e, e->drawExtent.width, e->drawExtent.height);
        init_depth_image(e, e->drawExtent.width, e->drawExtent.height);
    }
}
