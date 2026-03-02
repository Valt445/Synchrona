#include "engine.h"
#include "VkBootstrap.h"
#include "graphics_pipeline.h"
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <cstdint>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <types.h>
#include <vulkan/vulkan_core.h>
#include <iostream>
#include "loader.h"
#include <glm/packing.hpp>
#include "texture_loader.h"
// Define the global engine pointer declared as `extern` in the header

Engine* engine = nullptr;

// ------------------------- Initialization -------------------------
void init(Engine* e, uint32_t x, uint32_t y) {
    *e = Engine{};
    ::engine = e;
    e->sceneBasePath = "assets/Sponza/gLTF";
    init_vulkan(e);
    init_swapchain(e, x, y);
    init_descriptors(e);
    init_samplers(e);                    // ← EARLY
    create_draw_image(e, x, y);
    init_depth_image(e, x, y);
    init_commands(e);
    init_camera_buffers(e);
    init_sync_structures(e);
    init_pipelines(e);
    init_mesh_pipelines(e);
    init_default_data(e);
    init_imgui(e);
    init_debug_ui(e);

    setupCameraCallbacks(e->window);
    e->mainCamera.focusOn(glm::vec3(0.0f, 0.5f, 0.0f), 5.0f);
}


VkFormat find_depth_format(VkPhysicalDevice physicalDevice) {
    std::vector<VkFormat> candidates = { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT };

    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
        if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return format;
        }
    }
    std::cerr << "❌ No supported depth format found!\n";
    std::abort();
    return VK_FORMAT_UNDEFINED;
}

void destroy_depth_image(Engine* e) {
    if (e->depthImage.imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(e->device, e->depthImage.imageView, nullptr);
        e->depthImage.imageView = VK_NULL_HANDLE;
    }
    if (e->depthImage.image != VK_NULL_HANDLE) {
        vmaDestroyImage(e->allocator, e->depthImage.image, e->depthImage.allocation);
        e->depthImage.image = VK_NULL_HANDLE;
        e->depthImage.allocation = VK_NULL_HANDLE;
    }
    std::cout << "✅ Depth image destroyed\n";
}
// Create Vulkan instance, surface, device and queue

void init_camera_buffers(Engine* e) {
    for (int i = 0; i < FRAME_OVERLAP; ++i) {
        FrameData& frame = e->frames[i];
        frame.cameraBuffer = create_buffer(e->allocator, sizeof(CameraData),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);

        // Map persistently
        VK_CHECK(vmaMapMemory(e->allocator, frame.cameraBuffer.allocation, &frame.cameraBuffer.info.pMappedData));

        e->mainDeletionQueue.push_function([=]() {
            vmaUnmapMemory(e->allocator, frame.cameraBuffer.allocation);
            destroy_buffer(frame.cameraBuffer, e);
            });
    }
    std::cout << "✅ Camera UBOs mapped\n";
}

void init_depth_image(Engine* e, uint32_t width, uint32_t height) {
    destroy_depth_image(e); // Safe recreate

    e->depthImage.imageFormat = find_depth_format(e->physicalDevice);
    VkExtent3D depthExtent = { width, height, 1 };

    VkImageCreateInfo dimg_info = image_create_info(e->depthImage.imageFormat,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthExtent);

    VmaAllocationCreateInfo dimg_allocinfo = { .usage = VMA_MEMORY_USAGE_GPU_ONLY };
    dimg_allocinfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VkResult res = vmaCreateImage(e->allocator, &dimg_info, &dimg_allocinfo,
        &e->depthImage.image, &e->depthImage.allocation, nullptr);
    if (res != VK_SUCCESS) {
        std::cerr << "❌ Depth image creation failed: " << res << "\n";
        std::abort();
    }

    VkImageViewCreateInfo dview_info = imageview_create_info(
        e->depthImage.imageFormat, e->depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);

    VK_CHECK(vkCreateImageView(e->device, &dview_info, nullptr, &e->depthImage.imageView));

    e->mainDeletionQueue.push_function([=]() {
        destroy_depth_image(e);
        });

    std::cout << "✅ Depth image created with format " << e->depthImage.imageFormat << "\n";
}
void destroy_draw_image(Engine* e) {
    if (e->drawImage.imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(e->device, e->drawImage.imageView, nullptr);
        e->drawImage.imageView = VK_NULL_HANDLE;
    }
    if (e->drawImage.image != VK_NULL_HANDLE) {
        vmaDestroyImage(e->allocator, e->drawImage.image, e->drawImage.allocation);
        e->drawImage.image = VK_NULL_HANDLE;
        e->drawImage.allocation = VK_NULL_HANDLE;
    }

}

void init_debug_ui(Engine* e) {
    debug_ui_init(e);
    std::cout << "🐞 Goated Debug UI initialized!\n";
}

void init_samplers(Engine* e)
{
    // Linear sampler (for most textures)
    VkSamplerCreateInfo samplerInfo = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;

    VK_CHECK(vkCreateSampler(e->device, &samplerInfo, nullptr, &e->defaultSamplerLinear));

    // Nearest sampler (for UI / pixel-perfect)
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    VK_CHECK(vkCreateSampler(e->device, &samplerInfo, nullptr, &e->defaultSamplerNearest));

    std::cout << "✅ Samplers created (linear + nearest)\n";
}

// ====================== Updated create_draw_image (samplers moved out) ======================
void create_draw_image(Engine* e, uint32_t width, uint32_t height)
{
    destroy_draw_image(e);  // only destroys drawImage, NOT samplers

    VkExtent3D drawImageExtent = { width, height, 1 };
    e->drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    e->drawImage.imageExtent = drawImageExtent;
    e->drawExtent = { width, height };

    VkImageUsageFlags drawImageUsages = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_STORAGE_BIT |
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;

    VkImageCreateInfo rimg_info = image_create_info(e->drawImage.imageFormat, drawImageUsages, drawImageExtent);
    VmaAllocationCreateInfo rimg_allocinfo = { .usage = VMA_MEMORY_USAGE_GPU_ONLY };
    rimg_allocinfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VK_CHECK(vmaCreateImage(e->allocator, &rimg_info, &rimg_allocinfo,
        &e->drawImage.image, &e->drawImage.allocation, nullptr));

    VkImageViewCreateInfo rview_info = imageview_create_info(
        e->drawImage.imageFormat, e->drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK(vkCreateImageView(e->device, &rview_info, nullptr, &e->drawImage.imageView));

    // Bind draw image to bindless slot 0 (now safe)
    VkDescriptorImageInfo samplerInfo{};
    samplerInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    samplerInfo.imageView = e->drawImage.imageView;
    samplerInfo.sampler = e->defaultSamplerLinear;   // guaranteed valid

    VkDescriptorImageInfo storageInfo{};
    storageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    storageInfo.imageView = e->drawImage.imageView;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = e->bindlessSet;
    writes[0].dstBinding = 0;
    writes[0].dstArrayElement = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &samplerInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = e->bindlessSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &storageInfo;

    vkUpdateDescriptorSets(e->device, 2, writes, 0, nullptr);

    std::cout << "✅ Draw image bound to bindless slot 0\n";
}

void init_default_data(Engine* e)
{
    std::cout << "\n🔧 Initializing textures and meshes...\n";

    // Create fallback 1x1 images FIRST (indices 1, 2, 3 in bindless array)
    uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
    e->whiteImage = create_image((void*)&white, e, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
    upload_texture_to_bindless(e, e->whiteImage, e->defaultSamplerLinear, 1);

    uint32_t grey = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1));
    e->greyImage = create_image((void*)&grey, e, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
    upload_texture_to_bindless(e, e->greyImage, e->defaultSamplerLinear, 2);

    uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 1));
    e->blackImage = create_image((void*)&black, e, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
    upload_texture_to_bindless(e, e->blackImage, e->defaultSamplerLinear, 3);
    e->nextBindlessTextureIndex = 4; // reserve slots 0-3, loader starts here

    std::cout << "✅ Fallback textures uploaded to bindless array (indices 1-3)\n";

    // Load GLTF meshes - texture loading happens inside loadgltfMeshes
    auto testMeshes = loadgltfMeshes(e, "assets/Sponza/gLTF/Sponza.gltf");

    
    if (testMeshes.has_value()) {
        e->testMeshes = std::move(testMeshes.value());
        std::cout << "✅ Loaded " << e->testMeshes.size() << " meshes from GLTF\n";
    }
    else {
        std::cout << "⚠️  No meshes loaded from GLTF file\n";
        e->testMeshes.clear();
    }

    // Gradient fallback (for error texture)
    std::array<uint32_t, 256> gradientPixels;
    for (int i = 0; i < 256; i++) {
        float t = i / 255.0f;
        gradientPixels[i] = glm::packUnorm4x8(glm::vec4(t, t, t, 1.0f));
    }
    e->errrorImage = create_image(gradientPixels.data(), e,
        VkExtent3D{ 16, 16, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

    e->mainDeletionQueue.push_function([=]() {
        // Destroy any loaded mesh buffers
        for (auto& mesh : e->testMeshes) {
            if (mesh->meshBuffers.vertexBuffer.buffer != VK_NULL_HANDLE) {
                destroy_buffer(mesh->meshBuffers.vertexBuffer, e);
            }
            if (mesh->meshBuffers.indexBuffer.buffer != VK_NULL_HANDLE) {
                destroy_buffer(mesh->meshBuffers.indexBuffer, e);
            }
        }

        // Destroy images
        destroy_image(e->whiteImage, e);
        destroy_image(e->greyImage, e);
        destroy_image(e->blackImage, e);
        destroy_image(e->errrorImage, e);

        // Destroy samplers
        if (e->defaultSamplerNearest != VK_NULL_HANDLE) {
            vkDestroySampler(e->device, e->defaultSamplerNearest, nullptr);
            e->defaultSamplerNearest = VK_NULL_HANDLE;
        }
        if (e->defaultSamplerLinear != VK_NULL_HANDLE) {
            vkDestroySampler(e->device, e->defaultSamplerLinear, nullptr);
            e->defaultSamplerLinear = VK_NULL_HANDLE;
        }
        });
}

void engine_cleanup(Engine* e) {
    if (!e) return;

    vkDeviceWaitIdle(e->device);

    for (uint32_t i = 0; i < FRAME_OVERLAP; i++) {
        e->frames[i].deletionQueue.flush();
        vkDestroyCommandPool(e->device, e->frames[i].commandPool, nullptr);
        vkDestroySemaphore(e->device, e->frames[i].swapchainSemaphore, nullptr);
        vkDestroySemaphore(e->device, e->frames[i].renderSemaphore, nullptr);
        vkDestroyFence(e->device, e->frames[i].renderFence, nullptr);
    }

    // Destroy sceneTextures BEFORE mainDeletionQueue (which destroys the VMA allocator).
    // Destroying them after causes the VMA "allocations not freed" assertion.
    for (auto& tex : e->sceneTextures) {
        destroy_image(tex, e);
    }
    e->sceneTextures.clear();

    e->mainDeletionQueue.flush();

    for (auto view : e->swapchainImageViews) {
        vkDestroyImageView(e->device, view, nullptr);
    }

    destroy_depth_image(e);               // ← ADD THIS before mainDeletionQueue.flush()

    if (e->swapchain) vkDestroySwapchainKHR(e->device, e->swapchain, nullptr);
    if (e->device) vkDestroyDevice(e->device, nullptr);

    if (e->debug_messenger) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(e->instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func) {
            func(e->instance, e->debug_messenger, nullptr);
        }
        e->debug_messenger = VK_NULL_HANDLE;
    }

    if (e->surface) vkDestroySurfaceKHR(e->instance, e->surface, nullptr);
    if (e->instance) vkDestroyInstance(e->instance, nullptr);
    if (e->window) glfwDestroyWindow(e->window);

    // sceneTextures already destroyed before mainDeletionQueue above.
    glfwTerminate();

    std::cout << "Engine cleanup complete!" << std::endl;
}