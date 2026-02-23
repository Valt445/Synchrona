#include "engine.h"
#include <cstdio>
#include <iostream>

// Note: For complete code, see REFACTORING_DETAILED.md with line numbers
// This file contains: create_buffer, destroy_buffer, create_image, destroy_image

// Copy from engine_original.cpp the functions listed in REFACTORING_DETAILED.md:
// - create_buffer() - lines 248-294
// - destroy_buffer() - lines 296-299
// - create_image() (no data) - lines 421-455
// - create_image() (with data) - lines 457-505
// - destroy_image() - lines 507-515

// These functions handle:
// - VMA buffer allocation/deallocation
// - GPU image creation/destruction
// - Memory management and lifecycle
AllocatedBuffer create_buffer(
    VmaAllocator allocator,
    size_t allocSize,
    VkBufferUsageFlags usage,
    VmaMemoryUsage memoryUsage)
{
    AllocatedBuffer newBuffer{};

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = allocSize;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo vmaAllocInfo{};
    vmaAllocInfo.usage = memoryUsage;

    std::cerr << "➡️ Creating buffer: size=" << allocSize
        << " usage=" << usage
        << " memoryUsage=" << memoryUsage << std::endl;

    VkResult result = vmaCreateBuffer(
        allocator,
        &bufferInfo,
        &vmaAllocInfo,
        &newBuffer.buffer,
        &newBuffer.allocation,
        &newBuffer.info
    );

    if (result != VK_SUCCESS || newBuffer.buffer == VK_NULL_HANDLE) {
        std::cerr << "❌ Buffer creation FAILED! VkResult=" << result
            << " | size=" << allocSize
            << " | usage=" << usage
            << " | memoryUsage=" << memoryUsage
            << std::endl;
        newBuffer.buffer = VK_NULL_HANDLE;
        newBuffer.allocation = VK_NULL_HANDLE;
    }
    else {
        std::cerr << "✅ Buffer created successfully: "
            << "handle=" << newBuffer.buffer
            << " size=" << allocSize << std::endl;
    }

    return newBuffer;
}

void destroy_buffer(const AllocatedBuffer& buffer, Engine* e)
{
    vmaDestroyBuffer(e->allocator, buffer.buffer, buffer.allocation);
}

AllocatedImage create_image(Engine* e, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped)
{
    AllocatedImage newImage{};
    newImage.imageFormat = format;
    newImage.imageExtent = size;

    VkImageCreateInfo img_info = image_create_info(format, usage, size);
    if (mipmapped)
    {
        img_info.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;
    }

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(vmaCreateImage(e->allocator, &img_info, &alloc_info, &newImage.image, &newImage.allocation, nullptr));

    VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
    if (format == VK_FORMAT_D32_SFLOAT)
    {
        aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    VkImageViewCreateInfo view_info = imageview_create_info(format, newImage.image, aspectFlags);
    view_info.subresourceRange.levelCount = img_info.mipLevels;

    VK_CHECK(vkCreateImageView(e->device, &view_info, nullptr, &newImage.imageView));

    // Ownership: caller is responsible for destroying the image/imageView/allocation.
    // Do NOT register an automatic deletion lambda here to avoid double-free when
    // the caller also registers cleanup.
    return newImage;
}

// --- CHANGED: create_image (data upload) ---
AllocatedImage create_image(void* data, Engine* e, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped)
{
    size_t data_size = size.depth * size.height * size.width * 4; // 4 bytes per pixel for R8G8B8A8
    AllocatedBuffer uploadbuffer = create_buffer(
        e->allocator,
        data_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU
    );

    // Map explicitly and copy -- VMA may not provide pMappedData for this usage automatically.
    void* mapped = nullptr;
    VK_CHECK(vmaMapMemory(e->allocator, uploadbuffer.allocation, &mapped));
    memcpy(mapped, data, data_size);
    vmaUnmapMemory(e->allocator, uploadbuffer.allocation);

    // Fix: include TRANSFER_DST (was duplicated TRANSFER_SRC previously)
    AllocatedImage newImage = create_image(e, size, format, usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, mipmapped);

    immediate_submit([&](VkCommandBuffer cmd) {
        // Transition
        transition_image(cmd, newImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        // Copy
        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = size;

        vkCmdCopyBufferToImage(
            cmd,
            uploadbuffer.buffer,
            newImage.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &copyRegion
        );

        transition_image(cmd, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        }, e);

    destroy_buffer(uploadbuffer, e);
    return newImage;
}

void destroy_image(const AllocatedImage& image, Engine* e)
{
    if (image.imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(e->device, image.imageView, nullptr);
    }
    if (image.image != VK_NULL_HANDLE) {
        vmaDestroyImage(e->allocator, image.image, image.allocation);
    }
}