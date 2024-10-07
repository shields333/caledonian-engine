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
        m_availableBuffers.push_back((*commandBuffer)->getHandle());
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


void CommandBuffer::begin(VkCommandBufferUsageFlags flags)
{
    auto info = vkinit::command_buffer_begin_info(flags);
    VK_CHECK(vkBeginCommandBuffer(m_handle, &info));
}

void CommandBuffer::end()
{
    VK_CHECK(vkEndCommandBuffer(m_handle));
}

void CommandBuffer::barrier(Image& image, VkImageLayout newLayout, VkPipelineStageFlags2 stage, VkAccessFlagBits2 access)
{
    VkImageMemoryBarrier2 imageBarrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    imageBarrier.pNext = nullptr;

    imageBarrier.srcStageMask  = image.m_srcStage;
    imageBarrier.srcAccessMask = image.m_srcAccess;
    imageBarrier.dstStageMask  = image.m_srcStage = stage;
    imageBarrier.dstAccessMask = image.m_srcAccess = access;

    imageBarrier.oldLayout = image.m_layout;
    imageBarrier.newLayout = image.m_layout = newLayout;

    VkImageAspectFlags aspectMask = image.getAspectFlags();
    imageBarrier.subresourceRange = vkinit::image_subresource_range(aspectMask);
    imageBarrier.image = image.getHandle();

    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.pNext = nullptr;

    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &imageBarrier;

    vkCmdPipelineBarrier2(m_handle, &depInfo);
}

void CommandBuffer::clearColourImage(Image& image, VkClearColorValue clearColour, VkImageSubresourceRange& range)
{
    vkCmdClearColorImage(m_handle, image.getHandle(), image.m_layout, &clearColour, 1, &range);
}

