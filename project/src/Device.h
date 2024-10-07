#pragma once

#include "vk_types.h"

class Device
{
public:
	Device(VkDevice handle);
	~Device();

	Device(Device &other) = delete;
	Device operator=(Device &other) = delete;

	Device(Device&& other) noexcept { *this = std::move(other); }
	Device& operator=(Device &&other) noexcept;

	VkDevice getHandle() const { return m_handle; }

	VkImageView createImageView(const VkImageViewCreateInfo& info) const ;
	VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) const;

private:
	VkDevice m_handle;
};

