#include "Buffer.h"

#include "vk_engine.h"

Buffer::Buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
	// allocate buffer
	VkBufferCreateInfo bufferInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.pNext = nullptr;
	bufferInfo.size = allocSize;

	bufferInfo.usage = usage;

	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = memoryUsage;
	vmaallocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

	// allocate the buffer
	VK_CHECK(vmaCreateBuffer(VulkanEngine::get().m_allocator, &bufferInfo, &vmaallocInfo, &m_handle, &m_allocation,
		&m_info));
}

Buffer::~Buffer()
{
	vmaDestroyBuffer(VulkanEngine::get().m_allocator, m_handle, m_allocation);
}

Buffer::Buffer(Buffer&& other) noexcept
{
	*this = std::move(other);
}

Buffer& Buffer::operator=(Buffer&& other) noexcept
{
	m_handle = std::exchange(other.m_handle, VK_NULL_HANDLE);
	m_allocation = std::exchange(other.m_allocation, VK_NULL_HANDLE);
	m_info = std::move(other.m_info);
	return *this;
}
