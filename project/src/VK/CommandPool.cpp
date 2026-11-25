#include "CommandPool.h"

#include "vk_engine.h"
#include "vk_initializers.h"

CommandPool::CommandPool(uint32_t queueIdx, VkCommandPoolCreateFlags flags /* = 0 */) :
    m_flags(flags)
{
    VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(queueIdx, flags);
    VK_CHECK(vkCreateCommandPool(VulkanEngine::getDevice()->getHandle(), &commandPoolInfo, nullptr, &m_handle));
}

CommandPool::~CommandPool()
{
    vkDestroyCommandPool(VulkanEngine::getDevice()->getHandle(), m_handle, nullptr);
}

CommandPool::CommandPool(CommandPool&& other) noexcept
{
    *this = std::move(other);
}

CommandPool& CommandPool::operator=(CommandPool&& other) noexcept
{
    m_handle = std::exchange(other.m_handle, VK_NULL_HANDLE);

    m_allocatedBuffers = std::move(other.m_allocatedBuffers);
    m_availableBuffers = std::move(other.m_availableBuffers);
    m_pendingBuffers   = std::move(other.m_pendingBuffers);
    
    m_flags = other.m_flags;

    return *this;
}

CommandBuffer* CommandPool::requestCommandBuffer()
{
    if (m_availableBuffers.empty()) {
        VkCommandBuffer cmdBuf = allocateCommandBuffers();
        return &(m_allocatedBuffers.emplace(cmdBuf, cmdBuf).first->second);
    }
    else {
        VkCommandBuffer cmdBuf = m_availableBuffers.front();
        m_availableBuffers.pop_front();
        return &m_allocatedBuffers[cmdBuf];
    }
}

void CommandPool::returnCommandBuffer(CommandBuffer** commandBuffer)
{
    if (m_flags & VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT)
    {
        m_availableBuffers.push_back((*commandBuffer)->getHandle());
    }
    else
        m_pendingBuffers.push_back((*commandBuffer)->getHandle());

    *commandBuffer = nullptr;
}

VkCommandBuffer CommandPool::allocateCommandBuffers()
{
    return allocateCommandBuffers(1).front();
}

std::vector<VkCommandBuffer> CommandPool::allocateCommandBuffers(uint32_t count)
{
    VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(m_handle, count);

    std::vector<VkCommandBuffer> vkCommandBuffers(count);
    VK_CHECK(vkAllocateCommandBuffers(VulkanEngine::getDevice()->getHandle(), &cmdAllocInfo, vkCommandBuffers.data()));

    return vkCommandBuffers;
}

void CommandPool::resetPool()
{
    VK_CHECK(vkResetCommandPool(VulkanEngine::getDevice()->getHandle(), m_handle, 0));

    for (auto& cmdBuf : m_pendingBuffers)
        m_availableBuffers.push_back(cmdBuf);

    m_pendingBuffers.clear();
}


////////////// CommandBuffer ///////////////////

CommandBuffer::CommandBuffer(VkCommandBuffer commandBuffer) :
    m_handle(commandBuffer)
{
}


void CommandBuffer::reset(VkCommandBufferResetFlags flags)
{
    vkResetCommandBuffer(m_handle, flags);
}

void CommandBuffer::begin(VkCommandBufferUsageFlags flags)
{
    auto info = vkinit::command_buffer_begin_info(flags);
    VK_CHECK(vkBeginCommandBuffer(m_handle, &info));
}

void CommandBuffer::end()
{
    VK_CHECK(vkEndCommandBuffer(m_handle));
}

void CommandBuffer::barrier(Image& image, VkImageLayout newLayout, VkPipelineStageFlags2 stage, VkAccessFlagBits2 access, uint32_t mipLevel /*= 0*/, uint32_t levelCount /*= 1*/)
{
    VkImageMemoryBarrier2 imageBarrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    imageBarrier.pNext = nullptr;

    std::vector<VkImageMemoryBarrier2> barriers(levelCount, imageBarrier);

    for (auto& barrier : barriers) {
        auto& mipmap = image.getMipmap(mipLevel);

        barrier.srcStageMask = mipmap.srcStage;
        barrier.srcAccessMask = mipmap.srcAccess;
        barrier.dstStageMask = mipmap.srcStage = stage;
        barrier.dstAccessMask = mipmap.srcAccess = access;

        barrier.oldLayout = mipmap.layout;
        barrier.newLayout = mipmap.layout = newLayout;

        barrier.subresourceRange = vkinit::image_subresource_range(image.getAspectFlags());
        barrier.subresourceRange.baseMipLevel = mipLevel++;
        barrier.subresourceRange.levelCount = 1;
        barrier.image = image.getHandle();
    }

    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.pNext = nullptr;

    depInfo.imageMemoryBarrierCount = levelCount;
    depInfo.pImageMemoryBarriers = barriers.data();

    vkCmdPipelineBarrier2(m_handle, &depInfo);
}

void CommandBuffer::barrier(Buffer& buffer, VkPipelineStageFlags2 stage, VkAccessFlagBits2 access)
{
    VkBufferMemoryBarrier2 bufferBarrier{ .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };

    bufferBarrier.srcStageMask = buffer.srcStage;
    bufferBarrier.srcAccessMask = buffer.srcAccess;
    bufferBarrier.dstStageMask = buffer.srcStage = stage;
    bufferBarrier.dstAccessMask = buffer.srcAccess = access;

    //bufferBarrier.offset = buffer.getInfo().offset;
    bufferBarrier.size = VK_WHOLE_SIZE;
    bufferBarrier.buffer = buffer.getHandle();

    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.pNext = nullptr;

    depInfo.bufferMemoryBarrierCount = 1;
    depInfo.pBufferMemoryBarriers = &bufferBarrier;

    vkCmdPipelineBarrier2(m_handle, &depInfo);
}

void CommandBuffer::clearColourImage(Image& image, VkClearColorValue clearColour, VkImageSubresourceRange& range)
{
    vkCmdClearColorImage(m_handle, image.getHandle(), image.getMipmap(range.baseMipLevel).layout, &clearColour, 1, &range);
}

void CommandBuffer::blit(Image& srcImg, Image& dstImg)
{
    blit(srcImg, dstImg, srcImg.getExtent(), dstImg.getExtent());
}

void CommandBuffer::blit(Image& srcImg, Image& dstImg, VkExtent3D srcExtent, VkExtent3D dstExtent, uint32_t srcMip /*= 0*/, uint32_t dstMip /*= 0*/)
{
    VkImageBlit2 blitRegion{ .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2, .pNext = nullptr };

    blitRegion.srcOffsets[1].x = srcExtent.width;
    blitRegion.srcOffsets[1].y = srcExtent.height;
    blitRegion.srcOffsets[1].z = srcExtent.depth;

    blitRegion.dstOffsets[1].x = dstExtent.width;
    blitRegion.dstOffsets[1].y = dstExtent.height;
    blitRegion.dstOffsets[1].z = dstExtent.depth;

    blitRegion.srcSubresource.aspectMask = srcImg.getAspectFlags();
    blitRegion.srcSubresource.baseArrayLayer = 0;
    blitRegion.srcSubresource.layerCount = 1;
    blitRegion.srcSubresource.mipLevel = srcMip;

    blitRegion.dstSubresource.aspectMask = dstImg.getAspectFlags();
    blitRegion.dstSubresource.baseArrayLayer = 0;
    blitRegion.dstSubresource.layerCount = 1;
    blitRegion.dstSubresource.mipLevel = dstMip;

    VkBlitImageInfo2 blitInfo{ .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2, .pNext = nullptr };
    blitInfo.dstImage = dstImg.getHandle();
    blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    blitInfo.srcImage = srcImg.getHandle();
    blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    blitInfo.filter = VK_FILTER_LINEAR;
    blitInfo.regionCount = 1;
    blitInfo.pRegions = &blitRegion;

    vkCmdBlitImage2(m_handle, &blitInfo);
}

void CommandBuffer::copyToImage(Buffer& srcBuffer, Image& dstImg)
{
    VkExtent3D dstExtent(dstImg.getExtent().width, dstImg.getExtent().height, 1);

    VkBufferImageCopy copyRegion = {};
    copyRegion.bufferOffset = 0;
    copyRegion.bufferRowLength = 0;
    copyRegion.bufferImageHeight = 0;

    copyRegion.imageSubresource.aspectMask = dstImg.getAspectFlags();
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageExtent = dstExtent;

    vkCmdCopyBufferToImage(m_handle, srcBuffer.getHandle(), dstImg.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
        &copyRegion);

}

void CommandBuffer::bindPipeline(VkPipeline pipeline, VkPipelineBindPoint bindPoint)
{
    vkCmdBindPipeline(m_handle, bindPoint, pipeline);
}

void CommandBuffer::bindDescriptorSet(VkDescriptorSet set, VkPipelineLayout layout, VkPipelineBindPoint bindPoint)
{
    std::vector<VkDescriptorSet> sets(1, set);
    bindDescriptorSets(sets, layout, bindPoint);
}

void CommandBuffer::bindDescriptorSets(std::vector<VkDescriptorSet> &sets, VkPipelineLayout layout, VkPipelineBindPoint bindPoint)
{
    vkCmdBindDescriptorSets(m_handle, bindPoint, layout, 0, sets.size(), sets.data(), 0, nullptr);
}

void CommandBuffer::bindIndexBuffer(Buffer& buffer, VkIndexType type)
{
    vkCmdBindIndexBuffer(m_handle, buffer.getHandle(), 0, type);
}

void CommandBuffer::pushConstants(void* data, size_t range, VkPipelineLayout layout, VkShaderStageFlagBits stages)
{
    vkCmdPushConstants(m_handle, layout, stages, 0, range, data);
}

void CommandBuffer::setViewport(VkExtent2D extent)
{
    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = extent.width;
    viewport.height = extent.height;
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;

    vkCmdSetViewport(m_handle, 0, 1, &viewport);
}

void CommandBuffer::setScissor(VkExtent2D extent)
{
    VkRect2D scissor = {};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = extent.width;
    scissor.extent.height = extent.height;

    vkCmdSetScissor(m_handle, 0, 1, &scissor);
}

void CommandBuffer::dispatch(uint32_t x, uint32_t y, uint32_t z)
{
    vkCmdDispatch(m_handle, x, y, z);
}

void CommandBuffer::beginRendering(const Image* depth, VkAttachmentLoadOp depthLoadOp, VkAttachmentStoreOp depthStoreOp,
    const Image* colour, VkAttachmentLoadOp colourLoadOp, VkAttachmentStoreOp colourStoreOp, VkClearColorValue* clearColour /*= nullptr*/) const
{

    VkRenderingAttachmentInfo depthAttachment;
    VkRenderingAttachmentInfo colorAttachment;

    if (depth)
        depthAttachment = vkinit::depth_attachment_info(depth->getViewHandle(), VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, depthLoadOp, depthStoreOp);
    if (colour)
        colorAttachment = vkinit::attachment_info(colour->getViewHandle(), clearColour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, colourLoadOp, colourStoreOp);

    VkRenderingInfo renderInfo = vkinit::rendering_info(colour->getExtent2D(), 
        colour ? &colorAttachment : nullptr,
        depth ?  &depthAttachment : nullptr);

    vkCmdBeginRendering(m_handle, &renderInfo);
}

void CommandBuffer::endRendering()
{
    vkCmdEndRendering(m_handle);
}

void CommandBuffer::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{
    vkCmdDraw(m_handle, vertexCount, instanceCount, firstVertex, firstInstance);
}

void CommandBuffer::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, uint32_t vertexOffset, uint32_t firstInstance)
{
    vkCmdDrawIndexed(m_handle, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void CommandBuffer::drawIndirect(Buffer& drawsBuffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{
    vkCmdDrawIndexedIndirect(m_handle, drawsBuffer.getHandle(), offset, drawCount, stride);
}

void CommandBuffer::drawIndirectCount(Buffer& drawsBuffer, VkDeviceSize drawOffset, uint32_t maxDrawCount, uint32_t stride, Buffer& countBuffer, VkDeviceSize countOffset)
{
    vkCmdDrawIndexedIndirectCount(m_handle, drawsBuffer.getHandle(), drawOffset, countBuffer.getHandle(), countOffset, maxDrawCount, stride);
}

