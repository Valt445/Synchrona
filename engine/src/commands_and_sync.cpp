#include "engine.h"
#include <cstdio>

// ─── init_commands ────────────────────────────────────────────────────────────
// Creates command pools/buffers per frame + immediate submit resources.
// Camera buffers are created separately in init_camera_buffers (engine.cpp).
void init_commands(Engine* e) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = e->graphicsQueueFamily;

    for (uint32_t i = 0; i < FRAME_OVERLAP; i++) {
        VK_CHECK(vkCreateCommandPool(e->device, &poolInfo, nullptr,
            &e->frames[i].commandPool));

        VkCommandBufferAllocateInfo cmdAlloc{};
        cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAlloc.commandPool = e->frames[i].commandPool;
        cmdAlloc.commandBufferCount = 1;
        cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        VK_CHECK(vkAllocateCommandBuffers(e->device, &cmdAlloc,
            &e->frames[i].mainCommandBuffer));
    }

    // Immediate submit pool + buffer
    VK_CHECK(vkCreateCommandPool(e->device, &poolInfo, nullptr, &e->immCommandPool));

    VkCommandBufferAllocateInfo immAlloc{};
    immAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    immAlloc.commandPool = e->immCommandPool;
    immAlloc.commandBufferCount = 1;
    immAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    VK_CHECK(vkAllocateCommandBuffers(e->device, &immAlloc, &e->immCommandBuffer));

    e->mainDeletionQueue.push_function([=]() {
        vkDestroyCommandPool(e->device, e->immCommandPool, nullptr);
        // Per-frame pools are destroyed in engine_cleanup
        });

    std::printf("✅ Commands initialized\n");
}

// ─── Sync structures ──────────────────────────────────────────────────────────
VkFenceCreateInfo fence_create_info(VkFenceCreateFlags flags) {
    VkFenceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    info.flags = flags;
    return info;
}

VkSemaphoreCreateInfo semaphore_create_info(VkSemaphoreCreateFlags flags) {
    VkSemaphoreCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    info.flags = flags;
    return info;
}

void init_sync_structures(Engine* e) {
    VkFenceCreateInfo     fenceInfo = fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    VkSemaphoreCreateInfo semInfo = semaphore_create_info(0);

    for (uint32_t i = 0; i < FRAME_OVERLAP; i++) {
        VK_CHECK(vkCreateFence(e->device, &fenceInfo, nullptr, &e->frames[i].renderFence));
        VK_CHECK(vkCreateSemaphore(e->device, &semInfo, nullptr, &e->frames[i].swapchainSemaphore));
        VK_CHECK(vkCreateSemaphore(e->device, &semInfo, nullptr, &e->frames[i].renderSemaphore));
    }

    VK_CHECK(vkCreateFence(e->device, &fenceInfo, nullptr, &e->immFence));
    e->mainDeletionQueue.push_function([=]() {
        vkDestroyFence(e->device, e->immFence, nullptr);
        });

    std::printf("✅ Sync structures initialized\n");
}

// ─── Submit helpers ───────────────────────────────────────────────────────────
VkCommandBufferBeginInfo command_buffer_info(VkCommandBufferUsageFlags flags) {
    VkCommandBufferBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.flags = flags;
    return info;
}

VkSemaphoreSubmitInfo semaphore_submit_info(VkPipelineStageFlags2 stageMask,
    VkSemaphore semaphore) {
    VkSemaphoreSubmitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    info.semaphore = semaphore;
    info.stageMask = stageMask;
    info.deviceIndex = 0;
    info.value = 1;
    return info;
}

VkCommandBufferSubmitInfo command_buffer_submit_info(VkCommandBuffer cmd) {
    VkCommandBufferSubmitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    info.commandBuffer = cmd;
    info.deviceMask = 0;
    return info;
}

VkSubmitInfo2 submit_info(VkCommandBufferSubmitInfo* cmd,
    VkSemaphoreSubmitInfo* signalInfo,
    VkSemaphoreSubmitInfo* waitInfo) {
    VkSubmitInfo2 info{};
    info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    info.waitSemaphoreInfoCount = waitInfo ? 1u : 0u;
    info.pWaitSemaphoreInfos = waitInfo;
    info.signalSemaphoreInfoCount = signalInfo ? 1u : 0u;
    info.pSignalSemaphoreInfos = signalInfo;
    info.commandBufferInfoCount = 1;
    info.pCommandBufferInfos = cmd;
    return info;
}

// ─── get_current_frame ────────────────────────────────────────────────────────
// Single definition — always takes Engine* to avoid global dependency.
// Rendering.cpp also defines a local one that calls this — only ONE should exist.
// The declaration in engine.h ensures the Engine* version is used everywhere.
FrameData& get_current_frame(Engine* e) {
    return e->frames[e->frameNumber % FRAME_OVERLAP];
}