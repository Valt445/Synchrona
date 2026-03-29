#include "engine.h"
#include "graphics_pipeline.h"
#include "imgui.h"
#include <glm/ext/matrix_transform.hpp>
#include <chrono>
#include <glm/ext/matrix_clip_space.hpp>   // ← ADD THIS for glm::orthoZO

void update_uniform_buffers(Engine* e)
{
    FrameData& frame = get_current_frame(e);
    float aspect = (float)e->drawExtent.width / (float)e->drawExtent.height;

    // ── Camera ────────────────────────────────────────────────────────────────
    CameraData cam{};
    cam.view = e->mainCamera.getViewMatrix();
    cam.projection = e->mainCamera.getProjectionMatrix(aspect);
    cam.projection[1][1] *= -1.0f;
    cam.viewProjection = cam.projection * cam.view;
    cam.worldPosition = glm::vec4(e->mainCamera.position, 1.0f);

    // ── Light matrix — MUST match push.sunDirection exactly ──────────────────
    // Store on engine so draw_geometry and draw_shadow_pass both use same value
    glm::vec3 sunDir = glm::normalize(glm::vec3(0.3f, 1.0f, 0.4f));

    glm::mat4 lightView = glm::lookAt(
        sunDir * 80.0f,          // sun position — far enough to cover whole scene
        glm::vec3(0.0f),         // looking at scene centre
        glm::vec3(0.0f, 1.0f, 0.0f)
    );
    glm::mat4 lightProj = glm::orthoZO(   // ← change THIS word only
        -40.0f, 40.0f,
        -40.0f, 40.0f,
        1.0f, 300.0f
    );
    lightProj[1][1] *= -1.0f;    // Vulkan Y-flip — same as camera projection

    e->lightViewProj = lightProj * lightView;

    // store on engine for shadow pass
    cam.lightViewProj = e->lightViewProj;        // upload to UBO for PBR shader

    memcpy(frame.cameraBuffer.info.pMappedData, &cam, sizeof(CameraData));

    // ── Descriptor write (unchanged) ─────────────────────────────────────────
    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = frame.cameraBuffer.buffer;
    bufInfo.offset = 0;
    bufInfo.range = sizeof(CameraData);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = e->bindlessSet;
    write.dstBinding = 2;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &bufInfo;

    vkUpdateDescriptorSets(e->device, 1, &write, 0, nullptr);
}

void draw_geometry(Engine* e, VkCommandBuffer cmd)
{
    // Render geometry into 4x MSAA image, resolve into drawImage
    // Background (compute) already wrote into drawImage; geometry resolves on top
    VkRenderingAttachmentInfo colorAttachment{ .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    colorAttachment.imageView = e->msaaImage.imageView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;  // clear msaaImage each frame
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.clearValue = { {0.0f, 0.0f, 0.0f, 0.0f} };
    colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
    colorAttachment.resolveImageView = e->drawImage.imageView;
    colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkRenderingAttachmentInfo depthAttachment{ .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    depthAttachment.imageView = e->depthImage.imageView;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL_KHR;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil.depth = 1.0f;

    VkRenderingInfo renderInfo{ .sType = VK_STRUCTURE_TYPE_RENDERING_INFO };
    renderInfo.renderArea = { {0, 0}, {e->drawExtent.width, e->drawExtent.height} };
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorAttachment;
    renderInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderInfo);
    draw_skybox(e, cmd);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e->meshPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        e->meshPipelineLayout, 0, 1, &e->bindlessSet, 0, nullptr);

    VkViewport viewport{ 0, 0,
        (float)e->drawExtent.width, (float)e->drawExtent.height, 0.0f, 1.0f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{ {0, 0}, {e->drawExtent.width, e->drawExtent.height} };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    uint32_t drawCalls = 0;
    uint32_t triangles = 0;

    for (auto& asset : e->testMeshes) {
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &asset->meshBuffers.vertexBuffer.buffer, &offset);
        vkCmdBindIndexBuffer(cmd, asset->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);



        for (auto& surface : asset->surfaces) {
            MeshPushConstants push{};
            push.modelMatrix = asset->worldTransform;
            push.albedoIndex = surface.albedoIndex;
            push.normalIndex = surface.normalIndex;
            push.metalRoughIndex = surface.metallicRoughnessIndex;
            push.aoIndex = surface.aoIndex;
            push.emissiveIndex = surface.emissiveIndex;
            push.metallicFactor = surface.metallicFactor;
            push.roughnessFactor = surface.roughnessFactor;
            push.normalStrength = 1.0f;
            push.colorFactor = surface.colorFactor;
            push.sunDirection = normalize(glm::vec3(0.8f, 1.0f, 0.3f));
            push.sunIntensity = 2.5f;
            push.sunColor = glm::vec3(1.0f, 0.92f, 0.75f);
            push.shadowMapIndex = e->shadowMapBindlessIndex;  // = 5
            push.shadowBias = 0.003f;
            push.iblIrradianceIndex = e->iblIrradianceIndex;
            push.iblPrefilterIndex = e->iblPrefilterIndex;
            push.iblBrdfLutIndex = e->iblBrdfLutIndex;

            vkCmdPushConstants(cmd, e->meshPipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(MeshPushConstants), &push);

            vkCmdDrawIndexed(cmd, surface.count, 1, surface.startIndex, 0, 0);
            drawCalls++;
            triangles += surface.count / 3;
        }
    }

    e->lastDrawCalls = drawCalls;
    e->lastTriangles = triangles;

    
    vkCmdEndRendering(cmd);
}

void draw_background(VkCommandBuffer cmd, Engine* e)
{
    ComputeEffect& effect = e->backgroundEffects[e->currentBackgroundEffect];
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        e->gradientPipelineLayout, 0, 1, &e->bindlessSet, 0, nullptr);
    vkCmdPushConstants(cmd, e->gradientPipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ScenePushConstants), &effect.effectData);

    uint32_t dx = (e->drawImage.imageExtent.width + 15) / 16;
    uint32_t dy = (e->drawImage.imageExtent.height + 15) / 16;
    vkCmdDispatch(cmd, dx, dy, 1);
}

void draw_skybox(Engine* e, VkCommandBuffer cmd)
{
    VkViewport viewport{ 0, 0,
        (float)e->drawExtent.width, (float)e->drawExtent.height, 0.0f, 1.0f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{ {0, 0}, {e->drawExtent.width, e->drawExtent.height} };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    SkyPushConstants push{};
    push.sunDirection = normalize(glm::vec3(0.8f, 0.6f, 0.3f)); // match your sun
    push.time = e->skyTime;
    push.resolution = glm::vec2(e->drawExtent.width, e->drawExtent.height);
    push.cloudCoverage = e->cloudCoverage;
    push.cloudSpeed = e->cloudSpeed;
    

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e->skyboxPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        e->skyboxPipelineLayout, 0, 1, &e->bindlessSet, 0, nullptr);
    vkCmdPushConstants(cmd, e->skyboxPipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(SkyPushConstants), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0); 
    
}


void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView, Engine* e)
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    debug_ui_render(e);
    ImGui::Render();

    VkRenderingAttachmentInfo colorAttachment = attachment_info(
        targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderInfo = e->util.rendering_info(
        e->swapchainExtent, &colorAttachment, nullptr);

    vkCmdBeginRendering(cmd, &renderInfo);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRendering(cmd);
}

void engine_draw_frame(Engine* e)
{
    e->drawExtent.width  = e->swapchainExtent.width;
    e->drawExtent.height = e->swapchainExtent.height;

    static auto lastTime = std::chrono::high_resolution_clock::now();
    auto  now = std::chrono::high_resolution_clock::now();
    e->deltaTime = std::chrono::duration<float>(now - lastTime).count();
    lastTime = now;
    e->skyTime += e->deltaTime;

    debug_ui_update(e->deltaTime);
    e->mainCamera.update(e->window);

    if (e->resize_requested) {
        resize_swapchain(e);
        if (e->resize_requested) return;
    }
    if (e->swapchain == VK_NULL_HANDLE || e->swapchainImages.empty()) return;

    FrameData& frame = get_current_frame(e);
    VK_CHECK(vkWaitForFences(e->device, 1, &frame.renderFence, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(e->device, 1, &frame.renderFence));
    VK_CHECK(vkResetCommandPool(e->device, frame.commandPool, 0));

    uint32_t swapchainImageIndex;
    VkResult acquireResult = vkAcquireNextImageKHR(
        e->device, e->swapchain, UINT64_MAX,
        frame.swapchainSemaphore, VK_NULL_HANDLE, &swapchainImageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR) {
        e->resize_requested = true; return;
    }
    if (acquireResult != VK_SUCCESS) return;

    VkCommandBuffer cmd = frame.mainCommandBuffer;
    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    VkCommandBufferBeginInfo beginInfo = command_buffer_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    update_uniform_buffers(e);

    draw_shadow_pass(e, cmd);

    transition_image(cmd, e->drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
   

    VkImageMemoryBarrier2 depthBarrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    depthBarrier.image = e->depthImage.image;
    depthBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL_KHR;
    depthBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    depthBarrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
    depthBarrier.srcAccessMask = 0;
    depthBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
        | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthBarrier.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

    VkDependencyInfo dep{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &depthBarrier;
    vkCmdPipelineBarrier2(cmd, &dep);

    draw_geometry(e, cmd);

    transition_image(cmd, e->drawImage.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    transition_image(cmd, e->swapchainImages[swapchainImageIndex],
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    copy_image_to_image(cmd,
        e->drawImage.image, e->swapchainImages[swapchainImageIndex],
        e->drawImage.imageExtent, e->swapchainExtent);

    transition_image(cmd, e->swapchainImages[swapchainImageIndex],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    draw_imgui(cmd, e->swapchainImageViews[swapchainImageIndex], e);

    transition_image(cmd, e->swapchainImages[swapchainImageIndex],
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdInfo = command_buffer_submit_info(cmd);
    VkSemaphoreSubmitInfo     waitInfo = semaphore_submit_info(
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, frame.swapchainSemaphore);
    VkSemaphoreSubmitInfo     signalInfo = semaphore_submit_info(
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, frame.renderSemaphore);

    VkSubmitInfo2 submitInfo = submit_info(&cmdInfo, &signalInfo, &waitInfo);
    VK_CHECK(vkQueueSubmit2(e->graphicsQueue, 1, &submitInfo, frame.renderFence));

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &frame.renderSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &e->swapchain;
    presentInfo.pImageIndices = &swapchainImageIndex;

    VkResult presentResult = vkQueuePresentKHR(e->graphicsQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
        e->resize_requested = true;

    e->frameNumber++;
}

VkRenderingAttachmentInfo attachment_info(VkImageView view, VkClearValue* clear, VkImageLayout layout)
{
    VkRenderingAttachmentInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    info.imageView = view;
    info.imageLayout = layout;
    info.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    if (clear) info.clearValue = *clear;
    return info;
}

void draw_shadow_pass(Engine* e, VkCommandBuffer cmd)
{
    // ── Transition shadow map to depth write ──────────────────────────────────
    VkImageMemoryBarrier2 toWrite{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    toWrite.image = e->shadowMapImage.image;
    toWrite.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toWrite.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    toWrite.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    toWrite.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
    toWrite.srcAccessMask = 0;
    toWrite.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    toWrite.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

    VkDependencyInfo dep{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &toWrite;
    vkCmdPipelineBarrier2(cmd, &dep);

    // ── Begin depth-only rendering ────────────────────────────────────────────
    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = e->shadowMapImage.imageView;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil.depth = 1.0f;

    VkRenderingInfo renderInfo{};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.renderArea = { {0,0}, {2048, 2048} };
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 0;              // no colour
    renderInfo.pColorAttachments = nullptr;
    renderInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderInfo);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e->shadowPipeline);

    // Shadow map viewport — fixed 2048x2048
    VkViewport viewport{ 0, 0, 2048, 2048, 0.0f, 1.0f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{ {0,0}, {2048, 2048} };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // ── Draw all meshes from sun's POV ────────────────────────────────────────
    // NO descriptor set bind — shadowPipelineLayout has no sets
    for (auto& asset : e->testMeshes) {
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1,
            &asset->meshBuffers.vertexBuffer.buffer, &offset);
        vkCmdBindIndexBuffer(cmd, asset->meshBuffers.indexBuffer.buffer,
            0, VK_INDEX_TYPE_UINT32);

        for (auto& surface : asset->surfaces) {
            ShadowPushConstants push{};
            push.lightViewProj = e->lightViewProj;
            push.modelMatrix = asset->worldTransform;

            vkCmdPushConstants(cmd, e->shadowPipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT,
                0, sizeof(ShadowPushConstants), &push);

            vkCmdDrawIndexed(cmd, surface.count, 1, surface.startIndex, 0, 0);
        }
    }

    vkCmdEndRendering(cmd);

    // ── Transition to shader read for PBR pass ────────────────────────────────
    VkImageMemoryBarrier2 toRead{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    toRead.image = e->shadowMapImage.image;
    toRead.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
    toRead.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    toRead.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    toRead.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    toRead.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };

    VkDependencyInfo dep2{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep2.imageMemoryBarrierCount = 1;
    dep2.pImageMemoryBarriers = &toRead;
    vkCmdPipelineBarrier2(cmd, &dep2);
}