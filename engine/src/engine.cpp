#include "engine.h"
#include "VkBootstrap.h"
#include "graphics_pipeline.h"
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <types.h>
#include <vulkan/vulkan_core.h>
#include "loader.h"
#include <glm/packing.hpp>
#include "texture_loader.h"
#include "skybox.h"

Engine* engine = nullptr;

void init(Engine* e, uint32_t x, uint32_t y)
{
    *e = Engine{};
    ::engine = e;
    e->sceneBasePath = "assets/gLTF";
    e->shadowMapBindlessIndex = 5;
    
    init_vulkan(e);
    init_swapchain(e, x, y);
    init_descriptors(e);
    init_samplers(e);
    init_shadow_map(e, 2048, 2048);
    create_draw_image(e, x, y);
    init_depth_image(e, x, y);
    init_commands(e);
    init_camera_buffers(e);
    init_sync_structures(e);
    init_pipelines(e);
    init_skybox_pipelines(e);
    init_mesh_pipelines(e);
    init_shadow_pipeline(e);
    init_default_data(e);
    init_ibl(e);
    init_imgui(e);
    init_debug_ui(e);
    setupCameraCallbacks(e->window);
    e->mainCamera.focusOn(glm::vec3(0.0f, 0.5f, 0.0f), 5.0f);
}

VkFormat find_depth_format(VkPhysicalDevice physicalDevice)
{
    std::vector<VkFormat> candidates = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT
    };
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return format;
    }
    LOG_ERROR("No supported depth format found");
    std::abort();
    return VK_FORMAT_UNDEFINED;
}

void destroy_depth_image(Engine* e)
{
    if (e->depthImage.imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(e->device, e->depthImage.imageView, nullptr);
        e->depthImage.imageView = VK_NULL_HANDLE;
    }
    if (e->depthImage.image != VK_NULL_HANDLE) {
        vmaDestroyImage(e->allocator, e->depthImage.image, e->depthImage.allocation);
        e->depthImage.image = VK_NULL_HANDLE;
        e->depthImage.allocation = VK_NULL_HANDLE;
    }
}

void init_camera_buffers(Engine* e)
{
    for (int i = 0; i < FRAME_OVERLAP; ++i) {
        FrameData& frame = e->frames[i];
        frame.cameraBuffer = create_buffer(e->allocator, sizeof(CameraData),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);
        VK_CHECK(vmaMapMemory(e->allocator, frame.cameraBuffer.allocation,
            &frame.cameraBuffer.info.pMappedData));
        e->mainDeletionQueue.push_function([=]() {
            vmaUnmapMemory(e->allocator, frame.cameraBuffer.allocation);
            destroy_buffer(frame.cameraBuffer, e);
            });
    }
    LOG("Camera UBOs mapped");
}

void init_depth_image(Engine* e, uint32_t width, uint32_t height)
{
    destroy_depth_image(e);
    VkExtent3D depthExtent = { width, height, 1 };
    // create_msaa_image handles image + view creation with 4x MSAA sample count
    e->depthImage = create_msaa_image(e, depthExtent,
        VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
    e->mainDeletionQueue.push_function([=]() { destroy_depth_image(e); });
    LOG("Depth image created");
}

void init_shadow_map(Engine* e, uint32_t width, uint32_t height)
{


    e->shadowMapImage.imageFormat = VK_FORMAT_D32_SFLOAT; // hardcode, don't use find_depth_format
    // D32 guaranteed to support SAMPLED_BIT
    VkExtent3D shadowExtent = { width, height, 1 };
    e->shadowMapImage.imageExtent = shadowExtent;          // FIX 3: set extent

    VkImageCreateInfo smimg_info = image_create_info(
        e->shadowMapImage.imageFormat,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT,
        shadowExtent);

    VmaAllocationCreateInfo smimg_allocinfo{};
    smimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    smimg_allocinfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VK_CHECK(vmaCreateImage(e->allocator, &smimg_info, &smimg_allocinfo,
        &e->shadowMapImage.image, &e->shadowMapImage.allocation, nullptr));

    VkImageViewCreateInfo smview_info = imageview_create_info(
        e->shadowMapImage.imageFormat,
        e->shadowMapImage.image,
        VK_IMAGE_ASPECT_DEPTH_BIT);
    VK_CHECK(vkCreateImageView(e->device, &smview_info, nullptr,
        &e->shadowMapImage.imageView));

    // FIX 1: create the PCF sampler BEFORE using it
    VkSamplerCreateInfo samplerCI{ .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerCI.magFilter = VK_FILTER_LINEAR;
    samplerCI.minFilter = VK_FILTER_LINEAR;
    samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; // outside map = lit
    samplerCI.compareEnable = VK_FALSE;                            // PCF comparison
    samplerCI.compareOp = VK_COMPARE_OP_NEVER;
    samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    VK_CHECK(vkCreateSampler(e->device, &samplerCI, nullptr, &e->shadowMapSampler));

    // FIX 2: dstBinding=0 (texture array), dstArrayElement=shadowMapBindlessIndex (slot 5)
    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
    imgInfo.imageView = e->shadowMapImage.imageView;
    imgInfo.sampler = e->shadowMapSampler;   // now valid

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = e->bindlessSet;
    write.dstBinding = 0;                          // texture array
    write.dstArrayElement = e->shadowMapBindlessIndex;  // slot 5
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imgInfo;

    vkUpdateDescriptorSets(e->device, 1, &write, 0, nullptr);

    e->mainDeletionQueue.push_function([=]() {
        destroy_image(e->shadowMapImage, e);
        vkDestroySampler(e->device, e->shadowMapSampler, nullptr);
        });

    LOG("Shadow map created " << width << "x" << height
        << " at bindless slot " << e->shadowMapBindlessIndex);
}

void destroy_draw_image(Engine* e)
{
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

void init_debug_ui(Engine* e)
{
    debug_ui_init(e);
    LOG("Debug UI initialized");
}

void init_samplers(Engine* e)
{
    VkSamplerCreateInfo samplerInfo{ .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    VK_CHECK(vkCreateSampler(e->device, &samplerInfo, nullptr, &e->defaultSamplerLinear));

    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    VK_CHECK(vkCreateSampler(e->device, &samplerInfo, nullptr, &e->defaultSamplerNearest));

    LOG("Samplers created");
}

void create_draw_image(Engine* e, uint32_t width, uint32_t height)
{
    destroy_draw_image(e);

    VkExtent3D drawImageExtent = { width, height, 1 };
    e->drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    e->drawImage.imageExtent = drawImageExtent;
    e->drawExtent = { width, height };

    VkImageUsageFlags drawImageUsages =
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;

    VkImageCreateInfo rimg_info = image_create_info(e->drawImage.imageFormat, drawImageUsages, drawImageExtent);
    VmaAllocationCreateInfo rimg_allocinfo{};
    rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    rimg_allocinfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VK_CHECK(vmaCreateImage(e->allocator, &rimg_info, &rimg_allocinfo,
        &e->drawImage.image, &e->drawImage.allocation, nullptr));

    VkImageViewCreateInfo rview_info = imageview_create_info(
        e->drawImage.imageFormat, e->drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK(vkCreateImageView(e->device, &rview_info, nullptr, &e->drawImage.imageView));

    VkDescriptorImageInfo samplerInfo{};
    samplerInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    samplerInfo.imageView = e->drawImage.imageView;
    samplerInfo.sampler = e->defaultSamplerLinear;

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

    // msaaImage: COLOR_ATTACHMENT only — MSAA images CANNOT be storage images
    // Geometry renders into this 4x MSAA target, resolves into drawImage
    {
        VkImageCreateInfo msaa_info{};
        msaa_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        msaa_info.imageType = VK_IMAGE_TYPE_2D;
        msaa_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        msaa_info.extent = drawImageExtent;
        msaa_info.mipLevels = 1;
        msaa_info.arrayLayers = 1;
        msaa_info.samples = e->msaaSamples;
        msaa_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        msaa_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
            | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
        msaa_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo msaa_alloc{};
        msaa_alloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        msaa_alloc.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        VK_CHECK(vmaCreateImage(e->allocator, &msaa_info, &msaa_alloc,
            &e->msaaImage.image, &e->msaaImage.allocation, nullptr));

        VkImageViewCreateInfo msaa_view = imageview_create_info(
            VK_FORMAT_R16G16B16A16_SFLOAT, e->msaaImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
        VK_CHECK(vkCreateImageView(e->device, &msaa_view, nullptr, &e->msaaImage.imageView));

        e->msaaImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        e->msaaImage.imageExtent = drawImageExtent;
    }

    LOG("Draw image created — msaaImage(4x) -> resolve -> drawImage");
}

void init_default_data(Engine* e)
{
    LOG("Initializing default textures and scene...");

    uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
    e->whiteImage = create_image((void*)&white, e, { 1,1,1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
    upload_texture_to_bindless(e, e->whiteImage, e->defaultSamplerLinear, 1);

    uint32_t grey = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1));
    e->greyImage = create_image((void*)&grey, e, { 1,1,1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
    upload_texture_to_bindless(e, e->greyImage, e->defaultSamplerLinear, 2);

    uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 1));
    e->blackImage = create_image((void*)&black, e, { 1,1,1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
    upload_texture_to_bindless(e, e->blackImage, e->defaultSamplerLinear, 3);
    e->nextBindlessTextureIndex = e->shadowMapBindlessIndex + 1;

    auto testMeshes = loadgltfMeshes(e, "assets/gLTF/DamagedHelmet.gltf");
    if (testMeshes.has_value()) {
        e->testMeshes = std::move(testMeshes.value());
        LOG("Loaded " << e->testMeshes.size() << " meshes");
    }
    else {
        LOG_ERROR("No meshes loaded from GLTF file");
    }

    std::array<uint32_t, 256> gradientPixels;
    for (int i = 0; i < 256; i++) {
        float t = i / 255.0f;
        gradientPixels[i] = glm::packUnorm4x8(glm::vec4(t, t, t, 1.0f));
    }
    e->errrorImage = create_image(gradientPixels.data(), e,
        { 16, 16, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

    e->mainDeletionQueue.push_function([=]() {
        for (auto& mesh : e->testMeshes) {
            destroy_buffer(mesh->meshBuffers.vertexBuffer, e);
            destroy_buffer(mesh->meshBuffers.indexBuffer, e);
        }
        destroy_image(e->whiteImage, e);
        destroy_image(e->greyImage, e);
        destroy_image(e->blackImage, e);
        destroy_image(e->errrorImage, e);
        if (e->defaultSamplerNearest != VK_NULL_HANDLE)
            vkDestroySampler(e->device, e->defaultSamplerNearest, nullptr);
        if (e->defaultSamplerLinear != VK_NULL_HANDLE)
            vkDestroySampler(e->device, e->defaultSamplerLinear, nullptr);
        });
}

void engine_cleanup(Engine* e)
{
    if (!e) return;
    vkDeviceWaitIdle(e->device);

    for (uint32_t i = 0; i < FRAME_OVERLAP; i++) {
        e->frames[i].deletionQueue.flush();
        vkDestroyCommandPool(e->device, e->frames[i].commandPool, nullptr);
        vkDestroySemaphore(e->device, e->frames[i].swapchainSemaphore, nullptr);
        vkDestroySemaphore(e->device, e->frames[i].renderSemaphore, nullptr);
        vkDestroyFence(e->device, e->frames[i].renderFence, nullptr);
    }

    for (auto& tex : e->sceneTextures) destroy_image(tex, e);
    e->sceneTextures.clear();

    e->mainDeletionQueue.flush();

    for (auto view : e->swapchainImageViews)
        vkDestroyImageView(e->device, view, nullptr);

    destroy_depth_image(e);

    if (e->swapchain) vkDestroySwapchainKHR(e->device, e->swapchain, nullptr);
    if (e->device)    vkDestroyDevice(e->device, nullptr);

    if (e->debug_messenger) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(e->instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func) func(e->instance, e->debug_messenger, nullptr);
        e->debug_messenger = VK_NULL_HANDLE;
    }

    if (e->surface)   vkDestroySurfaceKHR(e->instance, e->surface, nullptr);
    if (e->instance)  vkDestroyInstance(e->instance, nullptr);
    if (e->window)    glfwDestroyWindow(e->window);

    glfwTerminate();
    LOG("Engine cleanup complete");
}