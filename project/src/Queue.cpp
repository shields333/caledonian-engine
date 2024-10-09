#include "Queue.h"

#include <vk_initializers.h>

#include "vk_engine.h"

Queue::Queue(VkQueue queue, uint32_t familyIdx) :
	m_handle(queue)
{
}

Fence Queue::submit(VkCommandBuffer cmdBuf,
					VkSemaphore signalSemaphore, VkPipelineStageFlags2 signalStageMask, 
					VkSemaphore waitSemaphore, VkPipelineStageFlags2 waitStageMask)
{
	VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmdBuf);

	VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(waitStageMask, waitSemaphore);
	VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(signalStageMask, signalSemaphore);

	VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, &signalInfo, &waitInfo);

	Fence fence = VulkanEngine::get().m_fencePool->requestFence();

	VK_CHECK(vkQueueSubmit2(m_handle, 1, &submit, fence.getHandle()));

	return fence;
}

void Queue::present(VkSwapchainKHR swapchain, uint32_t swapchainIndex, VkSemaphore waitSemaphore)
{
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;
	presentInfo.pSwapchains = &swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &waitSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainIndex;

	VK_CHECK(vkQueuePresentKHR(m_handle, &presentInfo));
}

