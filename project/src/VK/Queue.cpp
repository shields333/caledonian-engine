#include "Queue.h"
#include "vk_initializers.h"
#include "vk_engine.h"

Queue::Queue(VkQueue queue, uint32_t familyIdx) :
	m_handle(queue)
{
}

Fence Queue::submit(VkCommandBuffer cmdBuf,
					VkSemaphore signalSemaphore, VkPipelineStageFlags2 signalStageMask, 
					VkSemaphore waitSemaphore, VkPipelineStageFlags2 waitStageMask)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmdBuf);

	VkSemaphoreSubmitInfo* waitInfo = waitSemaphore ? new VkSemaphoreSubmitInfo(vkinit::semaphore_submit_info(waitStageMask, waitSemaphore)) : nullptr;
	VkSemaphoreSubmitInfo* signalInfo = signalSemaphore ? new VkSemaphoreSubmitInfo(vkinit::semaphore_submit_info(signalStageMask, signalSemaphore)) : nullptr;

	VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, signalInfo, waitInfo);

	Fence fence = VulkanEngine::get().m_fencePool->requestFence();

	VK_CHECK(vkQueueSubmit2(m_handle, 1, &submit, fence.getHandle()));

	delete waitInfo;
	delete signalInfo;

	return fence;
}

void Queue::present(VkSwapchainKHR swapchain, uint32_t swapchainIndex, VkSemaphore waitSemaphore)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;
	presentInfo.pSwapchains = &swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &waitSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainIndex;

	if (vkQueuePresentKHR(m_handle, &presentInfo) == VK_ERROR_OUT_OF_DATE_KHR)
		VulkanEngine::get().m_swapchain->resizeSwapchain();
}

