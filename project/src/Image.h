#pragma once 

#include <vk_types.h>

class CommandBuffer;

class Image
{
public:
	Image() = default;
	Image(VkFormat format, VkExtent3D extent, VkImageUsageFlags usageFlags, void* data = nullptr, bool generateMips = false);
	~Image();

	Image(const Image& other) = delete;
	Image operator=(const Image& other) = delete;

	Image(Image&& other) noexcept;
	Image& operator=(Image&& other) noexcept;

	struct Mipmap {
		VkImageLayout			layout = VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED;
		VkPipelineStageFlags2	srcStage = VK_PIPELINE_STAGE_2_NONE;
		VkAccessFlags2			srcAccess = VK_ACCESS_2_NONE;
	};

	VkImage		getHandle()		const { return m_handle; }
	VkImageView getViewHandle() const { return m_view; }

	VkFormat			getFormat()	const		{ return m_format; }
	VkImageAspectFlags	getAspectFlags() const	{ return m_aspectFlags; }
	VkExtent3D			getExtent() const		{ return m_extent; }
	VkExtent2D			getExtent2D() const { return VkExtent2D(m_extent.width, m_extent.height); }
	Mipmap&				getMipmap(uint32_t level) { return m_mipmaps[level]; }

	void uploadData(void* data, bool generateMips);
	void generateMipmaps(CommandBuffer& cmd);

protected:
	VkImage			m_handle		= VK_NULL_HANDLE;
	VmaAllocation	m_allocation	= 0;
	VkImageView		m_view			= VK_NULL_HANDLE;

	VkFormat			m_format	  = VkFormat::VK_FORMAT_UNDEFINED;
	VkExtent3D			m_extent	  = { 0, 0 };
	VkImageUsageFlags	m_usageFlags  = 0;
	VkImageAspectFlags	m_aspectFlags = 0;
	std::vector<Mipmap>	m_mipmaps;
};

class SwapchainImage : public Image
{
public:
	SwapchainImage(VkImage image, VkFormat format, VkExtent2D extent, VkImageUsageFlags usageFlags, VkImageAspectFlags aspectFlags, VkImageView view);
	~SwapchainImage();

	SwapchainImage(SwapchainImage&& other) noexcept;

};