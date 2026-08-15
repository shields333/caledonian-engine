#pragma once

#include "vk_types.h"

#include "Image.h"

class Swapchain
{
public:

	Swapchain(VkFormat format = VK_FORMAT_B8G8R8A8_UNORM);
	~Swapchain();

	Swapchain(const Swapchain& other) = delete;
	Swapchain& operator=(const Swapchain& other) = delete;
	
	Swapchain(Swapchain&& other) noexcept;
	Swapchain& operator=(Swapchain&& other) noexcept = delete;

	VkSwapchainKHR getHandle() const { return m_handle; }

	VkExtent2D  m_swapchainExtent;
	const	VkFormat	m_swapchainImageFormat;

	void createSwapchain();
	void destroySwapchain();

	std::vector<SwapchainImage>& getswapchainImages() { return m_swapchainImages; }

	uint32_t acquireImage(uint64_t timeout, VkSemaphore semaphore = VK_NULL_HANDLE);

	void resizeSwapchain();

private:

	VkSwapchainKHR m_handle;

	std::vector<SwapchainImage> m_swapchainImages;
};
