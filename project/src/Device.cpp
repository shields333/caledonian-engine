#include "Device.h"

#include <VkBootstrap.h>

#include "vk_initializers.h"

Device::Device(VkDevice handle) :
	m_handle(handle)
{
}

Device::~Device()
{
	vkDestroyDevice(m_handle, nullptr);
}

Device& Device::operator=(Device&& other) noexcept
{
	m_handle = std::exchange(other.m_handle, VK_NULL_HANDLE);

	return *this;
}

VkImageView Device::createImageView(const VkImageViewCreateInfo& info) const
{
	VkImageView view;
	VK_CHECK(vkCreateImageView(m_handle, &info, nullptr, &view));
	return view;
}

VkImageView Device::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) const
{
	auto viewInfo = vkinit::imageview_create_info(format, image, aspectFlags);
	return createImageView(viewInfo);
}
