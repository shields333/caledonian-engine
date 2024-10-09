#include "Swapchain.h"

#include "vk_engine.h"

#include <VkBootstrap.h>

Swapchain::Swapchain(VkExtent2D extent, VkFormat format /*= VK_FORMAT_B8G8R8A8_UNORM*/) :
    m_swapchainExtent(extent), m_swapchainImageFormat(format)
{
    vkb::SwapchainBuilder swapchainBuilder{ VulkanEngine::get().m_physicalDevice, VulkanEngine::getDevice()->getHandle(), VulkanEngine::get().m_surface };

    vkb::Swapchain vkbSwapchain = swapchainBuilder
        //.use_default_format_selection()
        .set_desired_format(VkSurfaceFormatKHR{ .format = m_swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
        //use vsync present mode
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(extent.width, extent.height)
        .add_image_usage_flags(VkImageUsageFlagBits::VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VkImageUsageFlagBits::VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .set_desired_min_image_count(3)
        .build()
        .value();

    //m_swapchainExtent = vkbSwapchain.extent;
    //store swapchain and its related images
    m_handle = vkbSwapchain.swapchain;

    auto swapchainImages = vkbSwapchain.get_images().value();
    auto swapchainImageViews = vkbSwapchain.get_image_views().value();

    m_swapchainImages.reserve(swapchainImages.size());
    for (int i = 0; i < swapchainImages.size(); i++) {
        m_swapchainImages.emplace_back(swapchainImages[i], vkbSwapchain.image_format, VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT,
            swapchainImageViews[i]);
    }
}

Swapchain::~Swapchain()
{
    vkDestroySwapchainKHR(VulkanEngine::getDevice()->getHandle(), m_handle, nullptr);

    // destroy swapchain resources
    m_swapchainImages.clear();
}

Swapchain::Swapchain(Swapchain&& other) noexcept :
    m_swapchainExtent(other.m_swapchainExtent), 
    m_swapchainImageFormat(other.m_swapchainImageFormat),
    m_swapchainImages(std::move(other.m_swapchainImages))
{
    m_handle = std::exchange(other.m_handle, VK_NULL_HANDLE);
}

uint32_t Swapchain::acquireImage(uint64_t timeout, VkSemaphore semaphore)
{
    uint32_t index = 0;
    VK_CHECK(vkAcquireNextImageKHR(VulkanEngine::getDevice()->getHandle(), m_handle, timeout, semaphore, nullptr, &index));
    return index;
}
