#pragma once

#include <vk_types.h>
#include <deque>

#include "Image.h"
#include "Buffer.h"

class CommandBuffer
{
public:
	CommandBuffer() = default;
	CommandBuffer(VkCommandBuffer commandBuffer);
	~CommandBuffer() = default;

	VkCommandBuffer getHandle() { return m_handle; }

	void reset(VkCommandBufferResetFlags flags = 0);
	void begin(VkCommandBufferUsageFlags flags = 0);
	void end();
	void barrier(Image& image, VkImageLayout newLayout, VkPipelineStageFlags2 stage, VkAccessFlagBits2 access, uint32_t mipLevel = 0, uint32_t levelCount = 1);
	void barrier(Buffer& buffer, VkPipelineStageFlags2 stage, VkAccessFlagBits2 access);
	void clearColourImage(Image& image, VkClearColorValue clearColour, VkImageSubresourceRange& range);
	void blit(Image& srcImg, Image& dstImg);
	void blit(Image& srcImg, Image& dstImg, VkExtent3D srcExtent, VkExtent3D dstExtent, uint32_t srcMip = 0, uint32_t dstMip = 0);
	void copyToImage(Buffer& srcBuffer, Image& dstImg);
	void bindPipeline(VkPipeline pipeline, VkPipelineBindPoint bindPoint);
	void bindDescriptorSet(VkDescriptorSet set, VkPipelineLayout layout, VkPipelineBindPoint bindPoint);
	void bindDescriptorSets(std::vector<VkDescriptorSet> &sets, VkPipelineLayout layout, VkPipelineBindPoint bindPoint);
	void bindIndexBuffer(Buffer& buffer, VkIndexType type = VK_INDEX_TYPE_UINT32);
	void pushConstants(void* data, size_t range, VkPipelineLayout layout, VkShaderStageFlagBits stages);
	void setViewport(VkExtent2D extent);
	void setScissor(VkExtent2D extent);
	void dispatch(uint32_t x, uint32_t y, uint32_t z);
	void beginRendering(const Image* depth, const Image* colour) const;
	//void beginRendering(const Image* depth, VkAttachmentLoadOp depthLoad, VkAttachmentStoreOp depthStore, const Image* colour, VkAttachmentLoadOp colourLoad, VkAttachmentStoreOp colourStore) const;
	void endRendering();
	void draw(uint32_t vertexCount, uint32_t instanceCount,uint32_t firstVertex, uint32_t firstInstance);
	void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, uint32_t vertexOffset, uint32_t firstInstance);
	void drawIndirect(Buffer& drawsBuffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride);
	void drawIndirectCount(Buffer& drawsBuffer, VkDeviceSize drawOffset, uint32_t maxDrawCount, uint32_t stride, Buffer& countBuffer, VkDeviceSize countOffset);

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
