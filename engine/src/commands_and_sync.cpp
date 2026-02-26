#include "engine.h"
#include <cstdio>

// Note: For complete code, see REFACTORING_DETAILED.md with line numbers
// This file contains: init_commands, init_sync_structures, and helper functions

// Copy from engine_original.cpp the functions listed in REFACTORING_DETAILED.md:
// - init_commands() - lines 835-871
// - fence_create_info() - lines 873-878  
// - semaphore_create_info() - lines 880-885
// - init_sync_structures() - lines 887-901
// - command_buffer_info() - lines 1150-1155
// - semaphore_submit_info() - lines 1157-1165
// - command_buffer_submit_info() - lines 1167-1173
// - submit_info() - lines 1175-1187
// - get_current_frame() - lines 1189-1192

// These functions handle:
// - Creating command pools and buffers per frame
// - Setting up synchronization (fences, semaphores)
// - Helper functions for Vulkan info structures
void init_commands(Engine* e) {
    VkCommandPoolCreateInfo commandPoolInfo{};
    commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolInfo.queueFamilyIndex = e->graphicsQueueFamily;

    for (uint32_t i = 0; i < FRAME_OVERLAP; i++) {
        VK_CHECK(vkCreateCommandPool(e->device, &commandPoolInfo, nullptr, &e->frames[i].commandPool));

        VkCommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAllocInfo.commandPool = e->frames[i].commandPool;
        cmdAllocInfo.commandBufferCount = 1;
        cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        VK_CHECK(vkAllocateCommandBuffers(e->device, &cmdAllocInfo, &e->frames[i].mainCommandBuffer));
    }

    VK_CHECK(vkCreateCommandPool(e->device, &commandPoolInfo, nullptr, &e->immCommandPool));

    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = e->immCommandPool;
    cmdAllocInfo.commandBufferCount = 1;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    VK_CHECK(vkAllocateCommandBuffers(e->device, &cmdAllocInfo, &e->immCommandBuffer));

    e->mainDeletionQueue.push_function([=]() {
        vkDestroyCommandPool(e->device, e->immCommandPool, nullptr);
        });

    std::printf("Commands initialized\n");
}



// ------------------------- Sync -------------------------
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
    VkFenceCreateInfo fenceCreateInfo = fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
    VkSemaphoreCreateInfo semaphoreCreateInfo = semaphore_create_info(0);

    for (uint32_t i = 0; i < FRAME_OVERLAP; i++) {
        VK_CHECK(vkCreateFence(e->device, &fenceCreateInfo, nullptr, &e->frames[i].renderFence));
        VK_CHECK(vkCreateSemaphore(e->device, &semaphoreCreateInfo, nullptr, &e->frames[i].swapchainSemaphore));
        VK_CHECK(vkCreateSemaphore(e->device, &semaphoreCreateInfo, nullptr, &e->frames[i].renderSemaphore));
    }

    VK_CHECK(vkCreateFence(e->device, &fenceCreateInfo, nullptr, &e->immFence));
    e->mainDeletionQueue.push_function([=]() { vkDestroyFence(e->device, e->immFence, nullptr); });
    std::printf("Sync structures initialized\n");
}


VkCommandBufferBeginInfo command_buffer_info(VkCommandBufferUsageFlags flags) {
    VkCommandBufferBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.flags = flags;
    return info;
}

VkSemaphoreSubmitInfo semaphore_submit_info(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore) {
    VkSemaphoreSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    submitInfo.semaphore = semaphore;
    submitInfo.stageMask = stageMask;
    submitInfo.deviceIndex = 0;
    submitInfo.value = 1;
    return submitInfo;
}

VkCommandBufferSubmitInfo command_buffer_submit_info(VkCommandBuffer cmd) {
    VkCommandBufferSubmitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    info.commandBuffer = cmd;
    info.deviceMask = 0;
    return info;
}

VkSubmitInfo2 submit_info(VkCommandBufferSubmitInfo* cmd,
    VkSemaphoreSubmitInfo* signalSemaphoreInfo,
    VkSemaphoreSubmitInfo* waitSemaphoreInfo) {
    VkSubmitInfo2 info{};
    info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    info.waitSemaphoreInfoCount = (waitSemaphoreInfo == nullptr) ? 0u : 1u;
    info.pWaitSemaphoreInfos = waitSemaphoreInfo;
    info.signalSemaphoreInfoCount = (signalSemaphoreInfo == nullptr) ? 0u : 1u;
    info.pSignalSemaphoreInfos = signalSemaphoreInfo;
    info.commandBufferInfoCount = 1;
    info.pCommandBufferInfos = cmd;
    return info;
}

FrameData& get_current_frame() {
    return engine->frames[engine->frameNumber % FRAME_OVERLAP];
}
