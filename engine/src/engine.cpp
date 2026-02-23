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

// Initialize the Engine
void init(Engine* e, uint32_t x, uint32_t y) {
    *e = Engine{};
    ::engine = e;

    init_vulkan(e);
    // Let init_swapchain() pick and set e->swapchainImageFormat consistently.
    init_swapchain(e, x, y);
    init_descriptors(e);
    create_draw_image(e, x, y);
    init_commands(e);
    init_sync_structures(e);
    init_pipelines(e);
    init_mesh_pipelines(e);
    init_default_data(e);
    init_imgui(e);

    // ── Camera setup ─────────────────────────────────────────────────────
    // Register all input callbacks (replaces the old key callback in init_vulkan).
    // Must be called AFTER init_vulkan sets the window user pointer to Engine*.
    setupCameraCallbacks(e->window);

    // Frame the loaded model. If you know your model's center/radius, pass them here.
    // These defaults work well for a car-sized object at the origin.
    e->mainCamera.focusOn(glm::vec3(0.0f, 0.5f, 0.0f), 5.0f);
    // Once you compute the real bounding box from your GLB, call:
    //   e->mainCamera.focusOn(bboxCenter, bboxRadius);
}

// Create Vulkan instance, surface, device and queue


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
    if (e->defaultSamplerLinear != VK_NULL_HANDLE) {
        vkDestroySampler(e->device, e->defaultSamplerLinear, nullptr);
        e->defaultSamplerLinear = VK_NULL_HANDLE;
    }
    if (e->defaultSamplerNearest != VK_NULL_HANDLE) {
        vkDestroySampler(e->device, e->defaultSamplerNearest, nullptr);
        e->defaultSamplerNearest = VK_NULL_HANDLE;
    }
}


void create_draw_image(Engine* e, uint32_t width, uint32_t height) {
    // 1. Destroy old resources
    destroy_draw_image(e);

    // 2. Setup Samplers (Usually better to move these to a one-time init_samplers function,
    // but having them here works as long as you destroy them in destroy_draw_image)
    VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sampl.magFilter = VK_FILTER_NEAREST;
    sampl.minFilter = VK_FILTER_NEAREST;
    vkCreateSampler(e->device, &sampl, nullptr, &e->defaultSamplerNearest);

    sampl.magFilter = VK_FILTER_LINEAR;
    sampl.minFilter = VK_FILTER_LINEAR;
    vkCreateSampler(e->device, &sampl, nullptr, &e->defaultSamplerLinear);

    // 3. Image Creation
    VkExtent3D drawImageExtent = { width, height, 1 };
    e->drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    e->drawImage.imageExtent = drawImageExtent;
    e->drawExtent = { width, height };

    VkImageUsageFlags drawImageUsages{};
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT; // Needed for Binding 1
    drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_SAMPLED_BIT; // Needed for Binding 0

    VkImageCreateInfo rimg_info = image_create_info(e->drawImage.imageFormat, drawImageUsages, drawImageExtent);
    VmaAllocationCreateInfo rimg_allocinfo = { .usage = VMA_MEMORY_USAGE_GPU_ONLY };
    rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(vmaCreateImage(e->allocator, &rimg_info, &rimg_allocinfo, &e->drawImage.image, &e->drawImage.allocation, nullptr));

    // 4. Image View Creation
    VkImageViewCreateInfo rview_info = imageview_create_info(e->drawImage.imageFormat, e->drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK(vkCreateImageView(e->device, &rview_info, nullptr, &e->drawImage.imageView));

    // --- NEW: DUAL BINDING UPDATE ---

    // Info for Binding 0: Reading via Sampler
    VkDescriptorImageInfo samplerInfo{};
    samplerInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    samplerInfo.imageView = e->drawImage.imageView;
    samplerInfo.sampler = e->defaultSamplerLinear;

    // Info for Binding 1: Writing via Storage Image
    VkDescriptorImageInfo storageInfo{};
    storageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    storageInfo.imageView = e->drawImage.imageView;

    VkWriteDescriptorSet drawImageWrites[2] = {};

    // Binding 0: COMBINED_IMAGE_SAMPLER array (textures) - slot 0 = draw image sampler
    drawImageWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    drawImageWrites[0].dstSet = e->bindlessSet;
    drawImageWrites[0].dstBinding = 0;
    drawImageWrites[0].dstArrayElement = 0;
    drawImageWrites[0].descriptorCount = 1;
    drawImageWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    drawImageWrites[0].pImageInfo = &samplerInfo;

    // Binding 1: STORAGE_IMAGE (draw image for compute write)
    drawImageWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    drawImageWrites[1].dstSet = e->bindlessSet;
    drawImageWrites[1].dstBinding = 1;
    drawImageWrites[1].descriptorCount = 1;
    drawImageWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    drawImageWrites[1].pImageInfo = &storageInfo;

    vkUpdateDescriptorSets(e->device, 2, drawImageWrites, 0, nullptr);

    std::cout << "Draw image updated: Binding 0 (Sampler) and Binding 1 (Storage) synced.\n";
}


void init_default_data(Engine* e)
{
    // Load GLTF meshes (includes the monkey head)
    auto testMeshes = loadgltfMeshes(e, "assets/ToyCar.glb");
    if (testMeshes.has_value()) {
        e->testMeshes = std::move(testMeshes.value());
        std::cout << "✓ Loaded " << e->testMeshes.size() << " meshes from GLTF (includes monkey)\n";
    }
    else {
        std::cout << "⚠️ No meshes loaded from GLTF file – monkey not found\n";
        e->testMeshes.clear();
    }


    // Create fallback 1x1 images (kept as safety)
    uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
    e->whiteImage = create_image((void*)&white, e, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
    upload_texture_to_bindless(e, e->whiteImage, e->defaultSamplerLinear, 1); // <-- add this

    uint32_t grey = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1));
    e->greyImage = create_image((void*)&grey, e, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
    upload_texture_to_bindless(e, e->whiteImage, e->defaultSamplerLinear, 1); // <-- add this

    uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 1));
    e->blackImage = create_image((void*)&black, e, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    upload_texture_to_bindless(e, e->whiteImage, e->defaultSamplerLinear, 1); // <-- add this

    // Gradient fallback
    std::array<uint32_t, 256> gradientPixels;
    for (int i = 0; i < 256; i++) {
        float t = i / 255.0f;
        gradientPixels[i] = glm::packUnorm4x8(glm::vec4(t, t, t, 1.0f));
    }
    e->errrorImage = create_image((void*)&gradientPixels, e,
        VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

    // Create samplers


    // Load MARBLE texture (replace with your marble image)
    AllocatedImage loadedTexture{};
    if (load_texture_from_file("assets/pavement.jpg", e, loadedTexture, e->defaultSamplerLinear)) {
        // Destroy fallback whiteImage and use marble texture
        if (e->whiteImage.image != VK_NULL_HANDLE) destroy_image(e->whiteImage, e);
        e->whiteImage = loadedTexture;
        std::cerr << "✅ Loaded assets/marble.jpg – monkey will be fancy marble!\n";
    }
    else {
        std::cerr << "⚠️ Could not load marble.jpg – using fallback white texture\n";
    }

    // Helper to update the descriptor set with the current texture
    auto update_single_image_descriptor = [&](Engine* e) {
        if (e->singleImageDescriptorSet == VK_NULL_HANDLE) return;
        VkDescriptorImageInfo setImgInfo{};
        setImgInfo.sampler = e->defaultSamplerLinear;
        setImgInfo.imageView = e->whiteImage.imageView;
        setImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet setWrite{};
        setWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        setWrite.dstSet = e->singleImageDescriptorSet;
        setWrite.dstBinding = 0;
        setWrite.dstArrayElement = 0;
        setWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        setWrite.descriptorCount = 1;
        setWrite.pImageInfo = &setImgInfo;

        vkUpdateDescriptorSets(e->device, 1, &setWrite, 0, nullptr);
        };

    // Allocate and update the single descriptor set for meshes
    if (e->singleImageDescriptorSetLayout != VK_NULL_HANDLE) {
        e->singleImageDescriptorSet = e->globalDescriptorAllocator.allocate(e->device, e->singleImageDescriptorSetLayout);
        update_single_image_descriptor(e);
    }
    else {
        std::cerr << "⚠️ singleImageDescriptorSetLayout is NULL – mesh texturing unavailable\n";
    }

    // Allocate ONCE and update with texture
    if (e->singleImageDescriptorSetLayout != VK_NULL_HANDLE) {
        e->meshTextureSet = e->globalDescriptorAllocator.allocate(
            e->device, e->singleImageDescriptorSetLayout
        );

        DescriptorWriter writer;
        writer.write_image(0, e->whiteImage.imageView, e->defaultSamplerLinear,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        writer.update_set(e->device, e->meshTextureSet);
    }

    e->mainDeletionQueue.push_function([=]() {
        // Destroy any loaded mesh buffers
        for (auto& mesh : e->testMeshes) {
            if (mesh->meshBuffers.vertexBuffer.buffer != VK_NULL_HANDLE) {
                destroy_buffer(mesh->meshBuffers.vertexBuffer, e->allocator);
            }
            if (mesh->meshBuffers.indexBuffer.buffer != VK_NULL_HANDLE) {
                destroy_buffer(mesh->meshBuffers.indexBuffer, e->allocator);
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