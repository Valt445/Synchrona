#include "ibl.h"
#include "engine.h"
#include <stb_image.h>

// ─── Load HDR from disk and upload to GPU ─────────────────────────────────────
AllocatedImage load_hdri(Engine* e, const char* filepath)
{
    int width, height, channels;
    float* data = stbi_loadf(filepath, &width, &height, &channels, 4);
    if (!data) {
        LOG("Failed to load HDRI: " << filepath);
        return {};
    }

    VkExtent3D extent{ (uint32_t)width, (uint32_t)height, 1 };
    size_t dataSize = (size_t)width * height * 4 * sizeof(float);

    AllocatedImage image = create_image(e, extent,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        false);

    image.imageExtent = extent;
    upload_image_data(e, image, (const void*)data, dataSize);
    stbi_image_free(data);

    LOG("HDRI loaded: " << width << "x" << height);
    return image;
}

// ─── Create a cubemap image with 6 array layers ───────────────────────────────
AllocatedImage create_cubemap_image(Engine* e, uint32_t size, VkFormat format,
    uint32_t mipLevels)
{
    AllocatedImage newImage{};
    newImage.imageFormat = format;
    newImage.imageExtent = { size, size, 1 };
    newImage.mipLevels = mipLevels;

    VkImageCreateInfo img_info{};
    img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType = VK_IMAGE_TYPE_2D;
    img_info.format = format;
    img_info.extent = { size, size, 1 };
    img_info.mipLevels = mipLevels;
    img_info.arrayLayers = 6;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    img_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VK_CHECK(vmaCreateImage(e->allocator, &img_info, &alloc_info,
        &newImage.image, &newImage.allocation, nullptr));

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = newImage.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = mipLevels;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 6;

    VK_CHECK(vkCreateImageView(e->device, &view_info, nullptr, &newImage.imageView));

    return newImage;
}

// ─── Helper: create a 2-binding descriptor set layout (sampler + storage) ─────
static VkDescriptorSetLayout make_ibl_layout(Engine* e, bool hasSamplerInput)
{
    uint32_t bindingCount = hasSamplerInput ? 2 : 1;
    VkDescriptorSetLayoutBinding bindings[2]{};

    if (hasSamplerInput) {
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    bindings[bindingCount - 1].binding = bindingCount - 1;
    bindings[bindingCount - 1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[bindingCount - 1].descriptorCount = 1;
    bindings[bindingCount - 1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = bindingCount;
    info.pBindings = bindings;

    VkDescriptorSetLayout layout;
    VK_CHECK(vkCreateDescriptorSetLayout(e->device, &info, nullptr, &layout));
    return layout;
}

// ─── Helper: create a compute pipeline ───────────────────────────────────────
static VkPipeline make_compute_pipeline(Engine* e, const char* shaderPath,
    VkPipelineLayout layout)
{
    VkShaderModule shader;
    if (!e->util.load_shader_module(shaderPath, e->device, &shader)) {
        LOG_ERROR("Failed to load shader: " << shaderPath);
        return VK_NULL_HANDLE;
    }

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = shader;
    stage.pName = "main";

    VkComputePipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage = stage;
    info.layout = layout;

    VkPipeline pipeline;
    VK_CHECK(vkCreateComputePipelines(e->device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline));
    vkDestroyShaderModule(e->device, shader, nullptr);
    return pipeline;
}

// ─── Helper: write a 2-binding descriptor set ────────────────────────────────
static void write_ibl_set(Engine* e, VkDescriptorSet set,
    VkImageView samplerView,   // binding 0 — can be VK_NULL_HANDLE for brdf lut
    VkImageView storageView,   // binding 1
    VkImageLayout samplerLayout = VK_IMAGE_LAYOUT_GENERAL,  
    VkImageLayout storageLayout = VK_IMAGE_LAYOUT_GENERAL)
{
    uint32_t writeCount = 0;
    VkWriteDescriptorSet writes[2]{};
    VkDescriptorImageInfo samplerInfo{};
    VkDescriptorImageInfo storageInfo{};

    if (samplerView != VK_NULL_HANDLE) {
        samplerInfo.sampler = e->defaultSamplerLinear;
        samplerInfo.imageView = samplerView;
        samplerInfo.imageLayout = samplerLayout;

        writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[writeCount].dstSet = set;
        writes[writeCount].dstBinding = 0;
        writes[writeCount].descriptorCount = 1;
        writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[writeCount].pImageInfo = &samplerInfo;
        writeCount++;
    }

    storageInfo.imageView = storageView;
    storageInfo.imageLayout = storageLayout;

    writes[writeCount].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[writeCount].dstSet = set;
    writes[writeCount].dstBinding = samplerView != VK_NULL_HANDLE ? 1 : 0;
    writes[writeCount].descriptorCount = 1;
    writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[writeCount].pImageInfo = &storageInfo;
    writeCount++;

    vkUpdateDescriptorSets(e->device, writeCount, writes, 0, nullptr);
}

static void upload_cubemap_to_bindless(Engine* e, AllocatedImage img,
    VkSampler sampler, uint32_t index)
{
    VkDescriptorImageInfo imgInfo{};
    imgInfo.sampler = sampler;
    imgInfo.imageView = img.imageView;
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = e->bindlessSet;
    write.dstBinding = 3;            // ← cubemap binding
    write.dstArrayElement = index;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imgInfo;

    vkUpdateDescriptorSets(e->device, 1, &write, 0, nullptr);
}

// ─── Main IBL init ────────────────────────────────────────────────────────────
void init_ibl(Engine* e)
{
    // ── 1. Load HDR ───────────────────────────────────────────────────────────
    e->hdrImage = load_hdri(e, "assets/suburban_soccer_park_8k.hdr");
    e->envCubemap = create_cubemap_image(e, 512, VK_FORMAT_R32G32B32A32_SFLOAT);
    e->irradianceMap = create_cubemap_image(e, 32, VK_FORMAT_R32G32B32A32_SFLOAT);
    e->prefilterMap = create_cubemap_image(e, 128, VK_FORMAT_R32G32B32A32_SFLOAT, 5);
    e->brdfLUT = create_image(e, { 512, 512, 1 }, VK_FORMAT_R16G16_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false);

    // ── 2. Descriptor set layouts ─────────────────────────────────────────────
    e->equirectSetLayout = make_ibl_layout(e, true);  // sampler + storage
    e->irradianceSetLayout = make_ibl_layout(e, true);  // sampler + storage
    e->prefilterSetLayout = make_ibl_layout(e, true);  // sampler + storage
    e->brdfLutSetLayout = make_ibl_layout(e, false); // storage only

    // ── 3. Pipeline layouts ───────────────────────────────────────────────────
    auto makePipelineLayout = [&](VkDescriptorSetLayout setLayout,
        bool hasPushConstant) -> VkPipelineLayout
        {
            VkPipelineLayoutCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            info.setLayoutCount = 1;
            info.pSetLayouts = &setLayout;

            VkPushConstantRange pushRange{};
            if (hasPushConstant) {
                pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                pushRange.offset = 0;
                pushRange.size = sizeof(float) + sizeof(uint32_t);
                info.pushConstantRangeCount = 1;
                info.pPushConstantRanges = &pushRange;
            }

            VkPipelineLayout layout;
            VK_CHECK(vkCreatePipelineLayout(e->device, &info, nullptr, &layout));
            return layout;
        };

    e->equirectLayout = makePipelineLayout(e->equirectSetLayout, false);
    e->irradianceLayout = makePipelineLayout(e->irradianceSetLayout, false);
    e->prefilterLayout = makePipelineLayout(e->prefilterSetLayout, true); // has push constant
    e->brdfLutLayout = makePipelineLayout(e->brdfLutSetLayout, false);

    // ── 4. Compute pipelines ──────────────────────────────────────────────────
    e->equirectPipeline = make_compute_pipeline(e,
        "shaders/equirect_to_cubemap.comp.spv", e->equirectLayout);
    e->irradiancePipeline = make_compute_pipeline(e,
        "shaders/irradiance.comp.spv", e->irradianceLayout);
    e->prefilterPipeline = make_compute_pipeline(e,
        "shaders/prefilter.comp.spv", e->prefilterLayout);
    e->brdfLutPipeline = make_compute_pipeline(e,
        "shaders/brdf_lut.comp.spv", e->brdfLutLayout);

    // ── 5. Allocate descriptor sets ───────────────────────────────────────────
    e->equirectSet = e->globalDescriptorAllocator.allocate(e->device, e->equirectSetLayout);
    e->irradianceSet = e->globalDescriptorAllocator.allocate(e->device, e->irradianceSetLayout);
    e->brdfLutSet = e->globalDescriptorAllocator.allocate(e->device, e->brdfLutSetLayout);

    // Prefilter needs one descriptor set per mip level
    // Each set points at a different mip-level image view of prefilterMap
    for (int mip = 0; mip < 5; mip++) {
        // Create a view for just this mip level
        VkImageViewCreateInfo mipView{};
        mipView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        mipView.image = e->prefilterMap.image;
        mipView.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        mipView.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        mipView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        mipView.subresourceRange.baseMipLevel = mip;  // ← specific mip
        mipView.subresourceRange.levelCount = 1;
        mipView.subresourceRange.baseArrayLayer = 0;
        mipView.subresourceRange.layerCount = 6;
        VK_CHECK(vkCreateImageView(e->device, &mipView, nullptr, &e->prefilterMipViews[mip]));

        e->prefilterSets[mip] = e->globalDescriptorAllocator.allocate(
            e->device, e->prefilterSetLayout);
    }

    // ── 6. Write descriptor sets ──────────────────────────────────────────────
    // equirect: HDR image → env cubemap
    write_ibl_set(e, e->equirectSet,
        e->hdrImage.imageView,
        e->envCubemap.imageView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,  // ← HDR layout
        VK_IMAGE_LAYOUT_GENERAL);

    // irradiance: envCubemap will be GENERAL during compute
    write_ibl_set(e, e->irradianceSet,
        e->envCubemap.imageView,
        e->irradianceMap.imageView,
        VK_IMAGE_LAYOUT_GENERAL,  // ← envCubemap layout
        VK_IMAGE_LAYOUT_GENERAL);
    // prefilter: env cubemap → prefilter mip views
    for (int mip = 0; mip < 5; mip++) {
        write_ibl_set(e, e->prefilterSets[mip],
            e->envCubemap.imageView,
            e->prefilterMipViews[mip],
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_GENERAL);
    }

    // brdf lut: no input, just output
    write_ibl_set(e, e->brdfLutSet,
        VK_NULL_HANDLE,
        e->brdfLUT.imageView);

    // ── 7. Run all compute passes once ───────────────────────────────────────
    immediate_submit([&](VkCommandBuffer cmd)
        {
            // Transition all outputs to GENERAL for compute writes
            transition_image(cmd, e->envCubemap.image,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
            transition_image(cmd, e->irradianceMap.image,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
            transition_image(cmd, e->prefilterMap.image,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
            transition_image(cmd, e->brdfLUT.image,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

            // ── equirect → env cubemap ────────────────────────────────────────────
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, e->equirectPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                e->equirectLayout, 0, 1, &e->equirectSet, 0, nullptr);
            vkCmdDispatch(cmd, 512 / 16, 512 / 16, 6);

            // Barrier: wait for env cubemap write before reading it
            VkImageMemoryBarrier2 barrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
            barrier.image = e->envCubemap.image;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, 6 };

            VkDependencyInfo dep{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(cmd, &dep);

            // ── irradiance convolution ────────────────────────────────────────────
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, e->irradiancePipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                e->irradianceLayout, 0, 1, &e->irradianceSet, 0, nullptr);
            vkCmdDispatch(cmd, 32 / 16, 32 / 16, 6);

            // Barrier: wait for irradiance write
            barrier.image = e->irradianceMap.image;
            vkCmdPipelineBarrier2(cmd, &dep);

            // ── prefilter — one dispatch per mip ──────────────────────────────────
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, e->prefilterPipeline);

            float    roughnessLevels[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
            uint32_t mipSize = 128;

            for (int mip = 0; mip < 5; mip++)
            {
                struct PrefilterPC { float roughness; uint32_t numSamples; };
                PrefilterPC pc{ roughnessLevels[mip], 1024u };
                vkCmdPushConstants(cmd, e->prefilterLayout,
                    VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PrefilterPC), &pc);

                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                    e->prefilterLayout, 0, 1, &e->prefilterSets[mip], 0, nullptr);

                uint32_t dispatch = std::max(1u, mipSize / 16);
                vkCmdDispatch(cmd, dispatch, dispatch, 6);

                mipSize /= 2;
            }

            // ── BRDF LUT ──────────────────────────────────────────────────────────
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, e->brdfLutPipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                e->brdfLutLayout, 0, 1, &e->brdfLutSet, 0, nullptr);
            vkCmdDispatch(cmd, 512 / 16, 512 / 16, 1);

            // Transition everything to SHADER_READ_ONLY for the PBR shader
            transition_image(cmd, e->envCubemap.image,
                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            transition_image(cmd, e->irradianceMap.image,
                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            transition_image(cmd, e->prefilterMap.image,
                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            transition_image(cmd, e->brdfLUT.image,
                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        }, e);

    e->iblIrradianceIndex = 0;  // slot 0 in cubemap array
    e->iblPrefilterIndex = 1;  // slot 1 in cubemap array
    // BRDF LUT stays in regular texture array binding 0
    e->iblBrdfLutIndex = 12; // stays in sampler2D array

    // ── 8. Register in bindless slots ─────────────────────────────────────────
    upload_cubemap_to_bindless(e, e->irradianceMap, e->defaultSamplerLinear, 0);
    upload_cubemap_to_bindless(e, e->prefilterMap, e->defaultSamplerLinear, 1);
    upload_texture_to_bindless(e, e->brdfLUT, e->defaultSamplerLinear, e->iblBrdfLutIndex);

    // ── 9. Cleanup pipelines — never needed again after startup ───────────────
    e->mainDeletionQueue.push_function([=]() {
        vkDestroyPipeline(e->device, e->equirectPipeline, nullptr);
        vkDestroyPipeline(e->device, e->irradiancePipeline, nullptr);
        vkDestroyPipeline(e->device, e->prefilterPipeline, nullptr);
        vkDestroyPipeline(e->device, e->brdfLutPipeline, nullptr);

        vkDestroyPipelineLayout(e->device, e->equirectLayout, nullptr);
        vkDestroyPipelineLayout(e->device, e->irradianceLayout, nullptr);
        vkDestroyPipelineLayout(e->device, e->prefilterLayout, nullptr);
        vkDestroyPipelineLayout(e->device, e->brdfLutLayout, nullptr);

        vkDestroyDescriptorSetLayout(e->device, e->equirectSetLayout, nullptr);
        vkDestroyDescriptorSetLayout(e->device, e->irradianceSetLayout, nullptr);
        vkDestroyDescriptorSetLayout(e->device, e->prefilterSetLayout, nullptr);
        vkDestroyDescriptorSetLayout(e->device, e->brdfLutSetLayout, nullptr);

        for (int i = 0; i < 5; i++)
            vkDestroyImageView(e->device, e->prefilterMipViews[i], nullptr);

        destroy_image(e->hdrImage, e);
        destroy_image(e->envCubemap, e);
        destroy_image(e->irradianceMap, e);
        destroy_image(e->prefilterMap, e);
        destroy_image(e->brdfLUT, e);
        });

    LOG("IBL initialized");
}