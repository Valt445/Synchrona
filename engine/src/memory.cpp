#include "engine.h"
#include <iostream>

AllocatedBuffer create_buffer(
    VmaAllocator       allocator,
    size_t             allocSize,
    VkBufferUsageFlags usage,
    VmaMemoryUsage     memoryUsage, Engine* e)
{
    AllocatedBuffer newBuffer{};

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = allocSize;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo vmaAllocInfo{};
    vmaAllocInfo.usage = memoryUsage;

    VkResult result = vmaCreateBuffer(
        allocator, &bufferInfo, &vmaAllocInfo,
        &newBuffer.buffer, &newBuffer.allocation, &newBuffer.info);

    if (result != VK_SUCCESS || newBuffer.buffer == VK_NULL_HANDLE) {
        LOG_ERROR("Buffer creation failed! VkResult=" << result
            << " size=" << allocSize << " usage=" << usage);
        newBuffer.buffer = VK_NULL_HANDLE;
        newBuffer.allocation = VK_NULL_HANDLE;
        VkBufferDeviceAddressInfo addrInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = newBuffer.buffer
        };
        newBuffer.address = vkGetBufferDeviceAddress(e->device, &addrInfo);
    }

    return newBuffer;
}

AllocatedImage create_msaa_image(Engine* e, VkExtent3D size, VkFormat format,
    VkImageUsageFlags usage)
{
    AllocatedImage newImage{};
    newImage.imageFormat = format;
    newImage.imageExtent = size;
    newImage.mipLevels = 1;

    VkImageCreateInfo img_info = image_create_info(format, usage, size);
    img_info.samples = e->msaaSamples; // ← hardcoded 4x
    img_info.mipLevels = 1;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VK_CHECK(vmaCreateImage(e->allocator, &img_info, &alloc_info,
        &newImage.image, &newImage.allocation, nullptr));

    VkImageAspectFlags aspectFlags = (format == VK_FORMAT_D32_SFLOAT)
        ? VK_IMAGE_ASPECT_DEPTH_BIT
        : VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageViewCreateInfo view_info = imageview_create_info(format, newImage.image, aspectFlags);
    view_info.subresourceRange.levelCount = 1;

    VK_CHECK(vkCreateImageView(e->device, &view_info, nullptr, &newImage.imageView));

    return newImage;
}

void destroy_buffer(const AllocatedBuffer& buffer, Engine* e)
{
    vmaDestroyBuffer(e->allocator, buffer.buffer, buffer.allocation);
}

AllocatedImage create_image(Engine* e, VkExtent3D size, VkFormat format,
    VkImageUsageFlags usage, bool mipmapped)
{
    AllocatedImage newImage{};
    newImage.imageFormat = format;
    newImage.imageExtent = size;

    VkImageCreateInfo img_info = image_create_info(format, usage, size);
    if (mipmapped)
        img_info.mipLevels = static_cast<uint32_t>(
            std::floor(std::log2(std::max(size.width, size.height)))) + 1;
        newImage.mipLevels = img_info.mipLevels;  // ← ADD THIS, was missing

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VK_CHECK(vmaCreateImage(e->allocator, &img_info, &alloc_info,
        &newImage.image, &newImage.allocation, nullptr));

    VkImageAspectFlags aspectFlags = (format == VK_FORMAT_D32_SFLOAT)
        ? VK_IMAGE_ASPECT_DEPTH_BIT
        : VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageViewCreateInfo view_info = imageview_create_info(format, newImage.image, aspectFlags);
    view_info.subresourceRange.levelCount = img_info.mipLevels;

    VK_CHECK(vkCreateImageView(e->device, &view_info, nullptr, &newImage.imageView));

    return newImage;
}

AllocatedImage create_image(void* data, Engine* e, VkExtent3D size, VkFormat format,
    VkImageUsageFlags usage, bool mipmapped)
{
    size_t data_size = (size_t)size.depth * size.height * size.width * 4;

    AllocatedBuffer uploadbuffer = create_buffer(
        e->allocator, data_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU, e);

    void* mapped = nullptr;
    VK_CHECK(vmaMapMemory(e->allocator, uploadbuffer.allocation, &mapped));
    memcpy(mapped, data, data_size);
    vmaUnmapMemory(e->allocator, uploadbuffer.allocation);

    AllocatedImage newImage = create_image(e, size, format,
        usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        mipmapped);

    immediate_submit([&](VkCommandBuffer cmd) {
        transition_image(cmd, newImage.image,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy copyRegion{};
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = size;

        vkCmdCopyBufferToImage(cmd, uploadbuffer.buffer, newImage.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        transition_image(cmd, newImage.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }, e);

    destroy_buffer(uploadbuffer, e);
    return newImage;
}

void destroy_image(const AllocatedImage& image, Engine* e)
{
    if (image.imageView != VK_NULL_HANDLE)
        vkDestroyImageView(e->device, image.imageView, nullptr);
    if (image.image != VK_NULL_HANDLE)
        vmaDestroyImage(e->allocator, image.image, image.allocation);
}