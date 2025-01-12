#pragma once

#include "vk_types.h"

class Buffer
{
public:
	Buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	~Buffer();

	Buffer(Buffer &&other) noexcept;
	Buffer& operator=(Buffer&& other) noexcept;

	VkBuffer					getHandle() const { return m_handle; }
	const VmaAllocation&		getAlloacation() const { return m_allocation; }
	const VmaAllocationInfo&	getInfo() const { return m_info; }
private:
	VkBuffer m_handle = VK_NULL_HANDLE;

	VmaAllocation		m_allocation = 0;
	VmaAllocationInfo	m_info;
};