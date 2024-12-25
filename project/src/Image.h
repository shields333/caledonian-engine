#pragma once 

#include <vk_types.h>

class Image
{
public:
	Image() = default;
	Image(VkFormat format, VkExtent2D extent, VkImageUsageFlags usageFlags, VkImageAspectFlags aspectFlags);
	~Image();

	Image(const Image& other) = delete;
	Image operator=(const Image& other) = delete;

	Image(Image&& other) noexcept;
	Image& operator=(Image&& other) noexcept;

	VkImageLayout			m_layout = VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED;
	VkPipelineStageFlags2	m_srcStage = VK_PIPELINE_STAGE_2_NONE;
	VkAccessFlags2			m_srcAccess = VK_ACCESS_2_NONE;

	VkImage		getHandle()		const { return m_handle; }
	VkImageView getViewHandle() const { return m_view; }

	VkFormat			getFormat()	const		{ return m_format; }
	VkImageAspectFlags	getAspectFlags() const	{ return m_aspectFlags; }
	VkExtent2D			getExtent() const		{ return m_extent; }

protected:
	VkImage			m_handle		= VK_NULL_HANDLE;
	VmaAllocation	m_allocation	= 0;
	VkImageView		m_view			= VK_NULL_HANDLE;

	VkFormat			m_format	  = VkFormat::VK_FORMAT_UNDEFINED;
	VkExtent2D			m_extent	  = { 0, 0 };
	VkImageUsageFlags	m_usageFlags  = 0;
	VkImageAspectFlags	m_aspectFlags = 0;
};

class SwapchainImage : public Image
{
public:
	SwapchainImage(VkImage image, VkFormat format, VkExtent2D extent, VkImageUsageFlags usageFlags, VkImageAspectFlags aspectFlags, VkImageView view);
	~SwapchainImage();

	SwapchainImage(SwapchainImage&& other) noexcept;

};