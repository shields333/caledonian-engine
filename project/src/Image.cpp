#include <Image.h>

#include <vk_initializers.h>

#include "vk_engine.h"

SwapchainImage::SwapchainImage(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) :
	m_handle(image), m_format(format), m_aspectFlags(aspectFlags)
{
	m_view = VulkanEngine::getDevice()->createImageView(image, format, aspectFlags);
}

SwapchainImage::SwapchainImage(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, VkImageView view) :
	m_handle(image), m_format(format), m_aspectFlags(aspectFlags), m_view(view)
{
}

SwapchainImage::~SwapchainImage()
{
	vkDestroyImageView(VulkanEngine::getDevice()->getHandle(), m_view, nullptr);
}

SwapchainImage::SwapchainImage(SwapchainImage&& other) noexcept
{
	*this = std::move(other);
}

SwapchainImage& SwapchainImage::operator=(SwapchainImage&& other) noexcept
{
	m_view = std::exchange(other.m_view, VK_NULL_HANDLE);
	m_handle = std::exchange(other.m_handle, VK_NULL_HANDLE);

	m_format = other.m_format;
	m_layout = other.m_layout;
	m_aspectFlags = other.m_aspectFlags;
	m_srcStage = other.m_srcStage;
	m_srcAccess = other.m_srcAccess;

	return *this;
}

Image::~Image()
{
	vkDestroyImage(VulkanEngine::getDevice()->getHandle(), m_handle, nullptr);
}
