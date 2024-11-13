#include <Image.h>

#include <vk_initializers.h>

#include "vk_engine.h"

Image::Image(VkFormat format, VkExtent2D extent, VkImageUsageFlags usageFlags, VkImageAspectFlags aspectFlags) :
	m_format(format), m_extent(extent), m_usageFlags(usageFlags), m_aspectFlags(aspectFlags)
{
	auto imageInfo = vkinit::image_create_info(format, usageFlags, VkExtent3D(extent.width, extent.height, 1));

	//for images, we want to allocate it from gpu local memory
	VmaAllocationCreateInfo allocinfo = {};
	allocinfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
	allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// create the image
	VK_CHECK(vmaCreateImage(VulkanEngine::get().m_allocator, &imageInfo, &allocinfo, &m_handle, &m_allocation, nullptr));

	// create the image's view
	auto viewInfo = vkinit::imageview_create_info(format, m_handle, aspectFlags);
	VK_CHECK(vkCreateImageView(VulkanEngine::getDevice()->getHandle(), &viewInfo, nullptr, &m_view));
}

Image::~Image()
{
	vkDestroyImageView(VulkanEngine::getDevice()->getHandle(), m_view, nullptr);
	vmaDestroyImage(VulkanEngine::get().m_allocator, m_handle, m_allocation);
}

Image::Image(Image&& other) noexcept
{
	*this = std::move(other);
}

Image& Image::operator=(Image&& other) noexcept
{
	m_view = std::exchange(other.m_view, VK_NULL_HANDLE);
	m_handle = std::exchange(other.m_handle, VK_NULL_HANDLE);

	m_extent = other.m_extent;
	m_format = other.m_format;
	m_usageFlags = other.m_usageFlags;
	m_aspectFlags = other.m_aspectFlags;

	m_layout = other.m_layout;
	m_srcStage = other.m_srcStage;
	m_srcAccess = other.m_srcAccess;

	return *this;
}

SwapchainImage::SwapchainImage(VkImage image, VkFormat format, VkExtent2D extent, VkImageUsageFlags usageFlags, VkImageAspectFlags aspectFlags, VkImageView view)
{
	m_handle = image;
	m_format = format,
	m_extent = extent;
	m_usageFlags = usageFlags;
	m_aspectFlags = aspectFlags;
	m_view = view;
}

SwapchainImage::~SwapchainImage()
{
	// swapchain deletes its own images
	m_handle = VK_NULL_HANDLE;
}

SwapchainImage::SwapchainImage(SwapchainImage&& other) noexcept
{
	*static_cast<Image*>(this) = static_cast<Image&&>(other);
}
