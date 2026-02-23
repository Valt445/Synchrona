#include "engine.h"
#include "graphics_pipeline.h"
#include "imgui.h"
#include <glm/ext/matrix_transform.hpp>

// Note: For complete code, see REFACTORING_DETAILED.md with line numbers
// This file contains: draw_background, draw_geometry, draw_imgui, engine_draw_frame, attachment_info

// Copy from engine_original.cpp the functions listed in REFACTORING_DETAILED.md:
// - attachment_info() - lines 819-833
// - draw_geometry() - lines 1217-1279
// - draw_background() - lines 1413-1433
// - draw_imgui() - lines 1434-1451
// - engine_draw_frame() - lines 1454-1567

// These functions handle:
// - Main frame rendering loop
// - Command buffer recording
// - Image layout transitions
// - Drawing geometry and effects
// - ImGui rendering

FrameData& get_current_frame(Engine* e) {
    return engine->frames[engine->frameNumber % FRAME_OVERLAP];
}


void draw_geometry(Engine* e, VkCommandBuffer cmd) {
    // 1. Setup Rendering Info
    VkRenderingAttachmentInfo colorAttachment = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    colorAttachment.imageView = e->drawImage.imageView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Keep the compute stars/background
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingAttachmentInfo depthAttachment = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    depthAttachment.imageView = e->depthImage.imageView;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil.depth = 1.0f; // Clear to "Far"

    VkRenderingInfo renderInfo = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO };
    renderInfo.renderArea = { 0, 0, (uint32_t)e->drawExtent.width, (uint32_t)e->drawExtent.height };
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorAttachment;
    renderInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(cmd, &renderInfo);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, e->meshPipeline);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        e->meshPipelineLayout, 0, 1, &e->bindlessSet, 0, nullptr);
    // ── Camera matrices ───────────────────────────────────────────────────
    float aspect = (float)e->drawExtent.width / (float)e->drawExtent.height;
    glm::mat4 view = e->mainCamera.getViewMatrix();
    glm::mat4 projection = e->mainCamera.getProjectionMatrix(aspect);
    projection[1][1] *= -1;   // Vulkan Y-flip

    glm::mat4 viewProj = projection * view;

    // Dynamic State
    VkViewport viewport = { 0, 0, (float)e->drawExtent.width, (float)e->drawExtent.height, 0, 1 };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor = { {0, 0}, {e->drawExtent.width, e->drawExtent.height} };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    for (auto& asset : e->testMeshes) {
        for (auto& surface : asset->surfaces) {
            MeshPushConstants push;

            // Static model matrix — orbit the camera instead of spinning the model.
            // To add per-object transforms later, store them in MeshAsset.
            glm::mat4 model = glm::mat4(1.0f);

            push.worldMatrix = projection * view * model;
            push.vertexBuffer = asset->meshBuffers.vertexBufferAddress;
            push.textureIndex = surface.albedoTextureIndex;
            vkCmdPushConstants(cmd, e->meshPipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(MeshPushConstants), &push);

            vkCmdBindIndexBuffer(cmd, asset->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, surface.count, 1, surface.startIndex, 0, 0);
        }
    }

    vkCmdEndRendering(cmd);
}
void draw_background(VkCommandBuffer cmd, Engine* e) {
    ComputeEffect& effect = e->backgroundEffects[e->currentBackgroundEffect];

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

    // FIX: Bind 'bindlessSet', not 'drawImageDescriptors'
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        e->gradientPipelineLayout, 0, 1,
        &e->bindlessSet, 0, nullptr);

    vkCmdPushConstants(cmd,
        e->gradientPipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(ScenePushConstants),
        &effect.effectData);

    uint32_t dispatchX = (e->drawImage.imageExtent.width + 15) / 16;
    uint32_t dispatchY = (e->drawImage.imageExtent.height + 15) / 16;
    vkCmdDispatch(cmd, dispatchX, dispatchY, 1);
}

void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView, Engine* e)
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();


    debug_ui_render();

    ImGui::Render();

    VkRenderingAttachmentInfo colorAttachment = attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderInfo = e->util.rendering_info(e->swapchainExtent, &colorAttachment, nullptr);

    vkCmdBeginRendering(cmd, &renderInfo);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRendering(cmd);
}

void engine_draw_frame(Engine* e) {
    // ── Update camera first — computes delta time and FPS movement ─────────
    e->mainCamera.update(e->window);

    // Handle resize BEFORE anything else
    if (e->resize_requested) {
        resize_swapchain(e);
        // If we just resized, skip this frame to avoid sync issues
        if (e->resize_requested) return;
    }

    // If swapchain is invalid (minimized), skip frame
    if (e->swapchain == VK_NULL_HANDLE || e->swapchainImages.empty()) {
        return;
    }

    FrameData& frame = get_current_frame(e);

    // Wait for previous frame to complete
    VK_CHECK(vkWaitForFences(e->device, 1, &frame.renderFence, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(e->device, 1, &frame.renderFence));
    VK_CHECK(vkResetCommandPool(e->device, frame.commandPool, 0));

    uint32_t swapchainImageIndex;
    VkResult acquireResult = vkAcquireNextImageKHR(
        e->device,
        e->swapchain,
        UINT64_MAX,
        frame.swapchainSemaphore,  // Use frame semaphore, not global
        VK_NULL_HANDLE,
        &swapchainImageIndex
    );

    // Handle swapchain out-of-date immediately
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR) {
        e->resize_requested = true;
        std::printf("🔄 Acquire indicated resize needed\n");
        return;
    }
    else if (acquireResult != VK_SUCCESS) {
        std::printf("❌ Acquire failed: %d\n", acquireResult);
        return;
    }

    VkCommandBuffer cmd = frame.mainCommandBuffer;
    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    VkCommandBufferBeginInfo cmdBeginInfo = command_buffer_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    // ... thy existing rendering code here (UNCHANGED) ...
    transition_image(cmd, e->drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    draw_background(cmd, e);
    transition_image(cmd, e->drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkImageMemoryBarrier2 depthBarrier = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    depthBarrier.image = e->depthImage.image;
    depthBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT; // Required for Depth formats
    depthBarrier.subresourceRange.levelCount = 1;
    depthBarrier.subresourceRange.layerCount = 1;

    VkDependencyInfo dep = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.pImageMemoryBarriers = &depthBarrier;
    dep.imageMemoryBarrierCount = 1;
    vkCmdPipelineBarrier2(cmd, &dep);

    draw_geometry(e, cmd);
    transition_image(cmd, e->drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    transition_image(cmd, e->swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copy_image_to_image(cmd, e->drawImage.image, e->swapchainImages[swapchainImageIndex],
        VkExtent3D{ e->drawImage.imageExtent.width, e->drawImage.imageExtent.height, 1 },
        e->swapchainExtent);
    transition_image(cmd, e->swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    draw_imgui(cmd, e->swapchainImageViews[swapchainImageIndex], e);
    transition_image(cmd, e->swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    VK_CHECK(vkEndCommandBuffer(cmd));

    // Submit using FRAME semaphores, not global ones
    VkCommandBufferSubmitInfo cmdInfo = command_buffer_submit_info(cmd);
    VkSemaphoreSubmitInfo waitInfo = semaphore_submit_info(
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        frame.swapchainSemaphore  // Wait on acquire
    );
    VkSemaphoreSubmitInfo signalInfo = semaphore_submit_info(
        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        frame.renderSemaphore     // Signal when done
    );

    VkSubmitInfo2 submit = submit_info(&cmdInfo, &signalInfo, &waitInfo);
    VK_CHECK(vkQueueSubmit2(e->graphicsQueue, 1, &submit, frame.renderFence));

    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &frame.renderSemaphore;  // Wait on render complete
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &e->swapchain;
    presentInfo.pImageIndices = &swapchainImageIndex;

    VkResult presentResult = vkQueuePresentKHR(e->graphicsQueue, &presentInfo);

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        e->resize_requested = true;
        std::printf("🔄 Present indicated resize needed\n");
    }
    else if (presentResult != VK_SUCCESS) {
        std::printf("❌ Present failed: %d\n", presentResult);
    }

    e->frameNumber++;
}

VkRenderingAttachmentInfo attachment_info(VkImageView view, VkClearValue* clear, VkImageLayout layout)
{
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.pNext = nullptr;
    colorAttachment.imageView = view;
    colorAttachment.imageLayout = layout;
    colorAttachment.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    if (clear) {
        colorAttachment.clearValue = *clear;
    }
    return colorAttachment;
}
