#pragma once

#include "vk_types.h"

class Buffer
{
public:
	Buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	~Buffer();

	Buffer(Buffer &&other) noexcept;
	Buffer& operator=(Buffer&& other) noexcept;

	VkBuffer getHandle() { return m_handle; }
	VmaAllocation getAlloacation() { return m_allocation; }

private:
	VkBuffer m_handle = VK_NULL_HANDLE;

	VmaAllocation		m_allocation = 0;
	VmaAllocationInfo	m_info;
};