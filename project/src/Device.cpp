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

void Device::waitIdle()
{
	vkDeviceWaitIdle(m_handle);
}
