#pragma once 

#include <vk_types.h>

class Image 
{
public:
	Image(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
	Image(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, VkImageView view);
	~Image();

	Image(const Image& other) = delete;
	Image operator=(const Image& other) = delete;

	Image(Image&& other) noexcept;
	Image& operator=(Image&& other) noexcept;

	VkImageLayout			m_layout = VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED;
	VkPipelineStageFlags2	m_srcStage = VK_PIPELINE_STAGE_2_NONE;
	VkAccessFlags2			m_srcAccess = VK_ACCESS_2_NONE;

	VkImage		getHandle()		{ return m_handle; }
	VkImageView getViewHandle() { return m_view; }

	VkFormat			getFormat()			{ return m_format; }
	VkImageAspectFlags	getAspectFlags()	{ return m_aspectFlags; }
private:
	VkImage		m_handle = VK_NULL_HANDLE;
	VkImageView m_view	 = VK_NULL_HANDLE;

	VkFormat			 m_format;
	VkImageAspectFlags	 m_aspectFlags;
};