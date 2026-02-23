#include "engine.h"
#include "VkBootstrap.h"
#include <iostream>
#include <algorithm>
#include <cstdio>

// Code extracted from original engine.cpp lines 592-833

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
    // Re-create draw image at the new size
    create_draw_image(e, newWidth, newHeight);

    std::printf("✅ Resize complete\n");
}
