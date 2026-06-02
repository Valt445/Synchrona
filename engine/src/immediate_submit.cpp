#include "engine.h"
#include <functional>


void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function, Engine* e)
{

    VK_CHECK(vkResetFences(e->device, 1, &e->immFence));
    VK_CHECK(vkResetCommandBuffer(e->immCommandBuffer, 0));

    VkCommandBuffer cmd = e->immCommandBuffer;

    VkCommandBufferBeginInfo cmdBeginInfo = command_buffer_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    function(cmd);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdinfo = command_buffer_submit_info(cmd);
    VkSubmitInfo2 submit = submit_info(&cmdinfo, nullptr, nullptr);

    VK_CHECK(vkQueueSubmit2(e->graphicsQueue, 1, &submit, e->immFence));

    VK_CHECK(vkWaitForFences(e->device, 1, &e->immFence, true, 9999999999));
}
