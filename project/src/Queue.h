#pragma once

#include <vk_types.h>

class Fence;

class Queue 
{
public:
	Queue(VkQueue queue, uint32_t familyIdx);
	~Queue() = default;

	const uint32_t	m_familyIndex = 0;

	VkQueue getHandle() { return m_handle; }

	Fence submit(VkCommandBuffer cmdBuf, 
				VkSemaphore signalSemaphore = VK_NULL_HANDLE, VkPipelineStageFlags2 signalStageMask = 0,
				VkSemaphore waitSemaphore = VK_NULL_HANDLE, VkPipelineStageFlags2 waitStageMask = 0);

	void present(VkSwapchainKHR swapchain, uint32_t swapchainIndex, VkSemaphore waitSemaphore = VK_NULL_HANDLE);

private:
	VkQueue		m_handle = VK_NULL_HANDLE;

};