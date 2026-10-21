#pragma once

#include "vk_types.h"

#include "Image.h"

class Swapchain
{
public:

	Swapchain(VkFormat format = VK_FORMAT_B8G8R8A8_SRGB);
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

	SwapchainImage& getswapchainImage() { return m_swapchainImages[m_swapchainIndex]; }
	VkSemaphore getSemaphore() const { return m_renderSemaphores[m_swapchainIndex]; }
	uint32_t getIndex() const { return m_swapchainIndex; }
	uint32_t getImageCount() const { return m_swapchainImages.size(); }

	uint32_t acquireImage(uint64_t timeout, VkSemaphore semaphore = VK_NULL_HANDLE);

	void resizeSwapchain();

private:

	VkSwapchainKHR m_handle;

	uint32_t m_swapchainIndex = 0;

	std::vector<VkSemaphore> m_renderSemaphores;

	std::vector<SwapchainImage> m_swapchainImages;
};
