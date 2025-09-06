#pragma once

#include <vk_types.h>
#include <deque>

class Fence
{
public:
	Fence(VkFence fence = VK_NULL_HANDLE) : m_handle(fence) {}
	~Fence();

	Fence(const Fence& other) = delete;
	Fence operator=(const Fence& other) = delete;

	Fence(Fence&& other) noexcept;
	Fence& operator=(Fence&& other) noexcept;

	VkFence getHandle() { return m_handle; }

	void		reset();
	VkResult	getStatus();
	VkResult	wait(uint64_t timeout);

private:
	VkFence m_handle = VK_NULL_HANDLE;
};

class FencePool
{
public:
	Fence requestFence(VkFenceCreateFlags flags = 0);
	void  returnFence(Fence& fence);

private:
	VkFence					createFences(VkFenceCreateFlags flags);
	std::vector<VkFence>	createFences(uint32_t count, VkFenceCreateFlags flags);

	std::deque<Fence> m_availableFences;
};