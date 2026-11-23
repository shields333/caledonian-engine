#include "Image.h"
#include "vk_initializers.h"
#include "vk_engine.h"
#include "CommandPool.h"

VkImageAspectFlags getAspectBits(VkFormat format)
{
	switch (format) {
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_X8_D24_UNORM_PACK32:
		return VK_IMAGE_ASPECT_DEPTH_BIT;

	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

	case VK_FORMAT_S8_UINT:
		return VK_IMAGE_ASPECT_STENCIL_BIT;

	default:
		return VK_IMAGE_ASPECT_COLOR_BIT;
	}
}

Image::Image(VkFormat format, VkExtent3D extent, VkImageUsageFlags usageFlags, void* data /* = nullptr */, bool generateMips /*= false*/) :
	m_format(format), m_extent(extent), m_usageFlags(usageFlags), m_aspectFlags(getAspectBits(format))
{
	if (data || generateMips) {
		m_usageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
	}
	auto imageInfo = vkinit::image_create_info(m_format, m_usageFlags, m_extent);

	if (generateMips)
		m_mipmaps.resize(imageInfo.mipLevels = int(std::floor(std::log2(std::max(m_extent.width, m_extent.height)))) + 1);
	else
		m_mipmaps.resize(1);

	//for images, we want to allocate it from gpu local memory
	VmaAllocationCreateInfo allocinfo = {};
	allocinfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
	allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// create the image
	VK_CHECK(vmaCreateImage(VulkanEngine::get().m_allocator, &imageInfo, &allocinfo, &m_handle, &m_allocation, nullptr));

	// create the image's view
	auto viewInfo = vkinit::imageview_create_info(format, m_handle, m_aspectFlags, imageInfo.mipLevels);
	VK_CHECK(vkCreateImageView(VulkanEngine::getDevice()->getHandle(), &viewInfo, nullptr, &m_view));

	// create views for each mip level individually
	for (int i = 0; i < m_mipmaps.size(); i++) {
		viewInfo = vkinit::imageview_create_info(format, m_handle, m_aspectFlags, 1, i);
		VK_CHECK(vkCreateImageView(VulkanEngine::getDevice()->getHandle(), &viewInfo, nullptr, &m_mipmaps[i].view));
	}

	if (data)
		uploadData(data, generateMips);
}

Image::~Image()
{
	for (auto& mipLevel : m_mipmaps)
		vkDestroyImageView(VulkanEngine::getDevice()->getHandle(), mipLevel.view, nullptr);

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

	m_mipmaps = std::move(other.m_mipmaps);

	return *this;
}

std::vector<VkImageView> Image::getMipMapViews() const
{
	std::vector<VkImageView> mipViews(m_mipmaps.size());
	for (int i = 0; i < mipViews.size(); i++)
		mipViews[i] = m_mipmaps[i].view;

	return mipViews;
}

void Image::uploadData(void* data, bool generateMips)
{
	size_t dataSize = m_extent.width * m_extent.height * 4;
	Buffer stagingBuffer(dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST);

	memcpy(stagingBuffer.getInfo().pMappedData, data, dataSize);

	VulkanEngine::get().immediateSubmit([&](CommandBuffer cmd) {
		cmd.barrier(*this, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR, VK_ACCESS_2_TRANSFER_WRITE_BIT);

		// copy the buffer into the image
		cmd.copyToImage(stagingBuffer, *this);
		
		cmd.barrier(*this, VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
		});
}

void Image::generateMipmaps()
{
	std::vector<VkImageView> mipViews(m_mipmaps.size() - 1);
	for (int i = 0; i < mipViews.size(); i++)
		mipViews[i] = m_mipmaps[i + 1].view;

	// create the downsampler descriptor set
	VkDescriptorSet downsamplerSet = VulkanEngine::get().m_globalDescriptorAllocator.allocate(VulkanEngine::get().m_downSampleModule->getDescriptorLayout());
	DescriptorWriter writer;
	writer.writeImage(0, m_mipmaps[0].view, VulkanEngine::get().m_defaultSamplerLinear->getHandle(), VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VkDescriptorType::VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	writer.writeImageArray(1, mipViews, VulkanEngine::get().m_defaultSamplerLinear->getHandle(), VkImageLayout::VK_IMAGE_LAYOUT_GENERAL, VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	writer.writeImage(2, mipViews[5], VulkanEngine::get().m_defaultSamplerLinear->getHandle(), VkImageLayout::VK_IMAGE_LAYOUT_GENERAL, VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	writer.update_set(VulkanEngine::getDevice()->getHandle(), downsamplerSet);

	VulkanEngine::get().immediateSubmit([&](CommandBuffer cmd) {
		VulkanEngine::get().m_downSampleModule->execute(&cmd, this, this, downsamplerSet, 1, mipViews.size());
		cmd.barrier(*this, VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, 0, mipViews.size());
	});

	VulkanEngine::get().m_globalDescriptorAllocator.clear_pools(VulkanEngine::getDevice()->getHandle());
}

SwapchainImage::SwapchainImage(VkImage image, VkFormat format, VkExtent2D extent, VkImageUsageFlags usageFlags, VkImageAspectFlags aspectFlags, VkImageView view)
{
	m_handle = image;
	m_format = format,
	m_extent = VkExtent3D(extent.width, extent.height, 1);
	m_usageFlags = usageFlags;
	m_aspectFlags = aspectFlags;
	m_view = view;

	m_mipmaps.resize(1);
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
