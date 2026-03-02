#include "engine.h"
#include "graphics_pipeline.h"
#include "imgui.h"
#include <glm/ext/matrix_transform.hpp>
#include <chrono>

// ─── update_uniform_buffers ───────────────────────────────────────────────────
// Called every frame before draw_geometry.
// Writes camera data to the CURRENT frame's buffer and updates the descriptor
// to point to that buffer. UPDATE_AFTER_BIND makes this safe even after binding.
void update_uniform_buffers(Engine* e)
{
    FrameData& frame = get_current_frame(e);
    float      aspect = (float)e->drawExtent.width / (float)e->drawExtent.height;

    CameraData cam{};
    cam.view = e->mainCamera.getViewMatrix();
    cam.projection = e->mainCamera.getProjectionMatrix(aspect);
    cam.projection[1][1] *= -1.0f;                          // Vulkan Y-flip
    cam.viewProjection = cam.projection * cam.view;
    cam.worldPosition = glm::vec4(e->mainCamera.position, 1.0f);

    // Write to this frame's persistently mapped buffer
    memcpy(frame.cameraBuffer.info.pMappedData, &cam, sizeof(CameraData));

    // Update the descriptor to point to THIS frame's buffer.
    // bindlessSet has UPDATE_AFTER_BIND so this is safe after binding.
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

// ─── draw_geometry ────────────────────────────────────────────────────────────
void draw_geometry(Engine* e, VkCommandBuffer cmd)
{
    // ── Rendering setup ───────────────────────────────────────────────────
    VkRenderingAttachmentInfo colorAttachment{ .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    colorAttachment.imageView = e->drawImage.imageView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;  // keep compute background
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

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
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e->meshPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        e->meshPipelineLayout, 0, 1, &e->bindlessSet, 0, nullptr);

    // ── Dynamic state ─────────────────────────────────────────────────────
    VkViewport viewport{ 0, 0,
        (float)e->drawExtent.width, (float)e->drawExtent.height, 0.0f, 1.0f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{ {0, 0}, {e->drawExtent.width, e->drawExtent.height} };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // ── Draw meshes ───────────────────────────────────────────────────────
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
            push.metallicFactor = 1.0f;  // Keep low for non-metal
            push.roughnessFactor = 1.0f; // ← FIXED: LOWER = SHINY/LESS GREY DIFFUSE
            push.pad = 0.0f;
            push.colorFactor = surface.colorFactor;
            push.sunDirection = normalize(glm::vec3(0.3f, 1.0f, 0.4f));
            push.sunColor = glm::vec3(1.0f, 0.98f, 0.95f); // ← FIXED: WARM WHITE (not dark blue)
            push.sunIntensity = 3.0f; // ← FIXED: HIGHER = BRIGHTER (play up to 100 if needed)
            push.normalStrength = 1.0f; // ← FIXED: STRONGER BUMPS
            vkCmdPushConstants(cmd, e->meshPipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(MeshPushConstants), &push);
            vkCmdDrawIndexed(cmd, surface.count, 1, surface.startIndex, 0, 0);
            std::cout << "Draw call: albedoIdx=" << push.albedoIndex
                << " normalIdx=" << push.normalIndex
                << " metalRoughIdx=" << push.metalRoughIndex
                << " aoIdx=" << push.aoIndex
                << " emissiveIdx=" << push.emissiveIndex << "\n";
            drawCalls++;
            triangles += surface.count / 3;
        }
    }

    // Expose stats to debug UI
    e->lastDrawCalls = drawCalls;
    e->lastTriangles = triangles;

    vkCmdEndRendering(cmd);
}

// ─── draw_background ─────────────────────────────────────────────────────────
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

// ─── draw_imgui ───────────────────────────────────────────────────────────────
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

// ─── engine_draw_frame ────────────────────────────────────────────────────────
void engine_draw_frame(Engine* e)
{
    // Delta time
    static auto lastTime = std::chrono::high_resolution_clock::now();
    auto  now = std::chrono::high_resolution_clock::now();
    e->deltaTime = std::chrono::duration<float>(now - lastTime).count();
    lastTime = now;

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

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR ||
        acquireResult == VK_SUBOPTIMAL_KHR) {
        e->resize_requested = true;
        return;
    }
    if (acquireResult != VK_SUCCESS) return;

    VkCommandBuffer cmd = frame.mainCommandBuffer;
    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    VkCommandBufferBeginInfo beginInfo = command_buffer_info(
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    // ── Upload uniforms ───────────────────────────────────────────────────
    update_uniform_buffers(e);

    // ── Render ───────────────────────────────────────────────────────────
    transition_image(cmd, e->drawImage.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    draw_background(cmd, e);

    transition_image(cmd, e->drawImage.image,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // Depth barrier
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

    // ── Blit to swapchain ─────────────────────────────────────────────────
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

    // ── Submit ────────────────────────────────────────────────────────────
    VkCommandBufferSubmitInfo cmdInfo = command_buffer_submit_info(cmd);
    VkSemaphoreSubmitInfo     waitInfo = semaphore_submit_info(
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, frame.swapchainSemaphore);
    VkSemaphoreSubmitInfo     signalInfo = semaphore_submit_info(
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, frame.renderSemaphore);

    VkSubmitInfo2 submitInfo = submit_info(&cmdInfo, &signalInfo, &waitInfo);
    VK_CHECK(vkQueueSubmit2(e->graphicsQueue, 1, &submitInfo, frame.renderFence));

    // ── Present ───────────────────────────────────────────────────────────
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &frame.renderSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &e->swapchain;
    presentInfo.pImageIndices = &swapchainImageIndex;

    VkResult presentResult = vkQueuePresentKHR(e->graphicsQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        presentResult == VK_SUBOPTIMAL_KHR) {
        e->resize_requested = true;
    }

    e->frameNumber++;
}

// ─── attachment_info ─────────────────────────────────────────────────────────
VkRenderingAttachmentInfo attachment_info(VkImageView view, VkClearValue* clear,
    VkImageLayout layout)
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