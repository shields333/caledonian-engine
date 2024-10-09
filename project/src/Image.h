#pragma once 

#include <vk_types.h>

class SwapchainImage
{
public:
	SwapchainImage(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
	SwapchainImage(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, VkImageView view);
	~SwapchainImage();

	SwapchainImage(const SwapchainImage& other) = delete;
	SwapchainImage operator=(const SwapchainImage& other) = delete;

	SwapchainImage(SwapchainImage&& other) noexcept;
	SwapchainImage& operator=(SwapchainImage&& other) noexcept;

	VkImageLayout			m_layout = VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED;
	VkPipelineStageFlags2	m_srcStage = VK_PIPELINE_STAGE_2_NONE;
	VkAccessFlags2			m_srcAccess = VK_ACCESS_2_NONE;

	VkImage		getHandle()		{ return m_handle; }
	VkImageView getViewHandle() { return m_view; }

	VkFormat			getFormat()			{ return m_format; }
	VkImageAspectFlags	getAspectFlags()	{ return m_aspectFlags; }

protected:
	VkImage		m_handle = VK_NULL_HANDLE;
	VkImageView m_view	 = VK_NULL_HANDLE;

	VkFormat			 m_format;
	VkImageAspectFlags	 m_aspectFlags;
};

class Image : public SwapchainImage
{
	~Image();
};