#pragma once

#include <vk_types.h>
#include <deque>

#include "Image.h"

class CommandBuffer
{
public:
	CommandBuffer() = default;
	CommandBuffer(VkCommandBuffer commandBuffer);
	~CommandBuffer() = default;

	VkCommandBuffer getHandle() { return m_handle; }

	void begin(VkCommandBufferUsageFlags flags = 0);
	void end();
	void barrier(Image& image, VkImageLayout newLayout, VkPipelineStageFlags2 stage, VkAccessFlagBits2 access);
	void clearColourImage(Image& image, VkClearColorValue clearColour, VkImageSubresourceRange& range);
	void blit(Image& srcImg, Image& dstImg);
	void bindPipeline(VkPipeline pipeline, VkPipelineBindPoint bindPoint);
	void bindDescriptorSet(VkDescriptorSet set, VkPipelineLayout layout, VkPipelineBindPoint bindPoint);
	void bindDescriptorSets(std::vector<VkDescriptorSet> &sets, VkPipelineLayout layout, VkPipelineBindPoint bindPoint);
	void dispatch(uint32_t x, uint32_t y, uint32_t z);

private:
	VkCommandBuffer m_handle = VK_NULL_HANDLE;
};

class CommandPool
{
public:
	CommandPool(uint32_t queueIdx, VkCommandPoolCreateFlags flags = 0);
	~CommandPool();

	CommandPool(CommandPool&& other) noexcept;
	CommandPool& operator=(CommandPool&& other) noexcept;

	// will allocate a new command buffer if none are available
	CommandBuffer* requestCommandBuffer();
	// returns a command buffer to the pool to be reused - will null the passed in pointer
	void		   returnCommandBuffer(CommandBuffer** commandBuffer);

	// resets the pool
	void resetPool();

private:

	VkCommandPoolCreateFlags	m_flags	 = 0;
	VkCommandPool				m_handle = VK_NULL_HANDLE;

	// holds any command buffers that have been returned to the pool and have been reset
	std::deque<VkCommandBuffer> m_availableBuffers;
	// holds any command buffers that have been returned to the pool and are pending a reset
	std::deque<VkCommandBuffer> m_pendingBuffers;

	// holds any command buffers that have been allocated
	std::unordered_map<VkCommandBuffer, CommandBuffer> m_allocatedBuffers;

	// allocates and returns a single command buffer
	VkCommandBuffer allocateCommandBuffers();
	// allocates and returns a vector of command buffers
	std::vector<VkCommandBuffer> allocateCommandBuffers(uint32_t count);
};
