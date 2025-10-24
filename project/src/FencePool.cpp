#include "FencePool.h"

#include "vk_engine.h"
#include "vk_initializers.h"

Fence FencePool::requestFence(VkFenceCreateFlags flags /* = 0 */)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_availableFences.empty() || flags & VK_FENCE_CREATE_SIGNALED_BIT) {
        return createFences(flags);
    }
    else {
        auto fence = std::move(m_availableFences.front());
        m_availableFences.pop_front();
        fence.reset();
        return fence;
    }
}

void FencePool::returnFence(Fence& fence)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_availableFences.push_back(std::move(fence));
}

VkFence FencePool::createFences(VkFenceCreateFlags flags)
{
    return createFences(1, flags).front();
}

std::vector<VkFence> FencePool::createFences(uint32_t count, VkFenceCreateFlags flags)
{
    std::vector<VkFenceCreateInfo> createInfo(count, vkinit::fence_create_info(flags));

    std::vector<VkFence> fences(count);
    VK_CHECK(vkCreateFence(VulkanEngine::getDevice()->getHandle(), createInfo.data(), nullptr, fences.data()));

    return fences;
}

/// <summary>
/// Fence
/// </summary>
Fence::~Fence()
{
    vkDestroyFence(VulkanEngine::getDevice()->getHandle(), m_handle, nullptr);
}

Fence::Fence(Fence&& other) noexcept
{
    *this = std::move(other);
}

Fence& Fence::operator=(Fence&& other) noexcept
{
    m_handle = std::exchange(other.m_handle, VK_NULL_HANDLE);
    return *this;
}

void Fence::reset()
{
    VK_CHECK(vkResetFences(VulkanEngine::getDevice()->getHandle(), 1, &m_handle));
}

VkResult Fence::getStatus()
{
    auto result = vkGetFenceStatus(VulkanEngine::getDevice()->getHandle(), m_handle);
    VK_CHECK(result);
    return result;
}

VkResult Fence::wait(uint64_t timeout)
{
    auto result = vkWaitForFences(VulkanEngine::getDevice()->getHandle(), 1, &m_handle, false, timeout);
    VK_CHECK(result);
    return result;
}
