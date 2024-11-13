//> includes
#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_initializers.h>

//bootstrap library
#include "VkBootstrap.h"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include <chrono>
#include <thread>

VulkanEngine* VulkanEngine::sm_loadedEngine = nullptr;

VulkanEngine& VulkanEngine::get() 
{ 
    if (sm_loadedEngine == nullptr)
        sm_loadedEngine = new VulkanEngine();

    return *sm_loadedEngine; 
}

const Device* VulkanEngine::getDevice()
{
    assert(sm_loadedEngine->m_device);
    return sm_loadedEngine->m_device.get();
}

VulkanEngine::VulkanEngine()
{
    // We initialize SDL and create a window with it.
    SDL_Init(SDL_INIT_VIDEO);

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);

    m_window = SDL_CreateWindow(
        "Vulkan Engine",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        m_windowExtent.width,
        m_windowExtent.height,
        window_flags);
}

VulkanEngine::~VulkanEngine()
{
    m_device->waitIdle();

    m_pipeline.reset();

    m_globalDescriptorAllocator.destroy_pool(m_device->getHandle());
    vkDestroyDescriptorSetLayout(m_device->getHandle(), m_drawImageDescriptorLayout, nullptr);

    for (auto& frame : m_frames) {
        vkDestroySemaphore(m_device->getHandle(), frame.renderSemaphore, nullptr);
        vkDestroySemaphore(m_device->getHandle(), frame.swapchainSemaphore, nullptr);
    }

    m_frames.clear();

    m_fencePool.reset();

    destroySwapchain();

    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);

    m_drawImage.reset();

    vmaDestroyAllocator(m_allocator);

    vkb::destroy_debug_utils_messenger(m_instance, m_debugMessenger);

    m_device.reset();

    vkDestroyInstance(m_instance, nullptr);

    SDL_DestroyWindow(m_window);
    
    // clear engine pointer
    sm_loadedEngine = nullptr;
}

void VulkanEngine::destroySwapchain()
{
    m_swapchain.reset();
}

void VulkanEngine::init()
{
    initVulkan();

    initSwapchain();

    initCommandPools();

    initSyncStructures();

    init_descriptors();

    init_pipelines();
}

void VulkanEngine::initVulkan()
{
    vkb::InstanceBuilder builder;

#if _DEBUG
    m_useValidationLayers = true;
#endif

    //make the vulkan instance, with basic debug features
    auto instRet = builder.set_app_name("Vulkan Application")
        .request_validation_layers(m_useValidationLayers)
        .use_default_debug_messenger()
        .require_api_version(1, 3, 0)
        .build();

    vkb::Instance vkbInst = instRet.value();

    //grab the instance 
    m_instance = vkbInst.instance;
    m_debugMessenger = vkbInst.debug_messenger;

    SDL_Vulkan_CreateSurface(m_window, m_instance, &m_surface);

    //vulkan 1.3 features
    VkPhysicalDeviceVulkan13Features features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    features.dynamicRendering = true;
    features.synchronization2 = true;

    //vulkan 1.2 features
    VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing = true;

    //use vkbootstrap to select a gpu. 
    //We want a gpu that can write to the SDL surface and supports vulkan 1.3 with the correct features
    vkb::PhysicalDeviceSelector selector{ vkbInst };
    vkb::PhysicalDevice physicalDevice = selector
        .set_minimum_version(1, 3)
        .set_required_features_13(features)
        .set_required_features_12(features12)
        .set_surface(m_surface)
        .select()
        .value();

    //create the final vulkan device
    vkb::DeviceBuilder deviceBuilder{ physicalDevice };

    vkb::Device vkbDevice = deviceBuilder.build().value();

    // Get the VkDevice handle used in the rest of a vulkan application
    m_device = std::make_unique<Device>(vkbDevice.device);
    m_physicalDevice = physicalDevice.physical_device;

    // use vkbootstrap to get a Graphics queue
    m_graphicsQueue = std::make_unique<Queue>(vkbDevice.get_queue(vkb::QueueType::graphics).value(),
        vkbDevice.get_queue_index(vkb::QueueType::graphics).value());

    // initialize the memory allocator
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = m_physicalDevice;
    allocatorInfo.device = m_device->getHandle();
    allocatorInfo.instance = m_instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocatorInfo, &m_allocator);
}

void VulkanEngine::initSwapchain()
{
    m_swapchain = std::make_unique<Swapchain>(m_windowExtent);
    m_frameCount = m_swapchain->getswapchainImages().size();

    VkImageUsageFlags drawImageUsages{};
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    m_drawImage = std::make_unique<Image>(VK_FORMAT_B10G11R11_UFLOAT_PACK32, m_windowExtent, drawImageUsages, VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT);
}

void VulkanEngine::initCommandPools()
{
    m_frames.resize(m_frameCount);

    for (auto& frame : m_frames) {
        frame.commandPool = std::make_unique<CommandPool>(m_graphicsQueue->m_familyIndex);
        frame.mainCommandBuffer = frame.commandPool->requestCommandBuffer();
    }

    m_fencePool = std::make_unique<FencePool>();
}

void VulkanEngine::initSyncStructures()
{
    //create syncronization structures
    //2 semaphores to syncronize rendering with swapchain
    VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

    for (int i = 0; i < m_frameCount; i++) {
        VK_CHECK(vkCreateSemaphore(m_device->getHandle(), &semaphoreCreateInfo, nullptr, &m_frames[i].swapchainSemaphore));
        VK_CHECK(vkCreateSemaphore(m_device->getHandle(), &semaphoreCreateInfo, nullptr, &m_frames[i].renderSemaphore));
    }
}

void VulkanEngine::init_descriptors()
{
    //create a descriptor pool that will hold 10 sets with 1 image each
    std::vector<DescriptorAllocator::PoolSizeRatio> sizes =
    {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }
    };

    m_globalDescriptorAllocator.init_pool(m_device->getHandle(), 10, sizes);

    //make the descriptor set layout for our compute draw
    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        m_drawImageDescriptorLayout = builder.build(m_device->getHandle(), VK_SHADER_STAGE_COMPUTE_BIT);
    }

    //allocate a descriptor set for our draw image
    m_drawImageDescriptors = m_globalDescriptorAllocator.allocate(m_device->getHandle(), m_drawImageDescriptorLayout);

    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imgInfo.imageView = m_drawImage->getViewHandle();

    VkWriteDescriptorSet drawImageWrite = {};
    drawImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    drawImageWrite.pNext = nullptr;

    drawImageWrite.dstBinding = 0;
    drawImageWrite.dstSet = m_drawImageDescriptors;
    drawImageWrite.descriptorCount = 1;
    drawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    drawImageWrite.pImageInfo = &imgInfo;

    vkUpdateDescriptorSets(m_device->getHandle(), 1, &drawImageWrite, 0, nullptr);
}

void VulkanEngine::init_pipelines()
{
    init_background_pipelines();
}

void VulkanEngine::init_background_pipelines()
{
    std::vector<VkDescriptorSetLayout> layouts(1, m_drawImageDescriptorLayout);
    m_pipeline = std::make_unique<ComputePipeline>("../project/shaders/gradient.comp.spv", layouts);
}

void VulkanEngine::recordCommands()
{
    auto& frameData = m_frames[m_frameNumber];

    if (!frameData.fences.empty()) {
        for (auto& fence : frameData.fences) {
            fence.wait(UINT64_MAX);
            fence.reset();
            m_fencePool->returnFence(fence);
        }
        frameData.fences.clear();
    }

    // acquire the next swapchain image
    m_swapchainIndex = m_swapchain->acquireImage(UINT64_MAX, frameData.swapchainSemaphore);
    Image& swapchainImage = static_cast<Image&>(m_swapchain->getswapchainImages()[m_swapchainIndex]);

    // reset the pool before recording
    frameData.commandPool->resetPool();

    auto& cmd = frameData.mainCommandBuffer;
    cmd->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    // transition image for transfer
    cmd->barrier(*m_drawImage, VkImageLayout::VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT);

    cmd->bindPipeline(m_pipeline->getHandle(), VK_PIPELINE_BIND_POINT_COMPUTE);
    cmd->bindDescriptorSet(m_drawImageDescriptors, m_pipeline->getLayout(), VK_PIPELINE_BIND_POINT_COMPUTE);
    cmd->dispatch(std::ceil(m_windowExtent.width / 16.0), std::ceil(m_windowExtent.height / 16.0), 1);

    // transition images for copying
    cmd->barrier(*m_drawImage, VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    cmd->barrier(swapchainImage, VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    // copy offscreen image to swapchain image
    cmd->blit(*m_drawImage, swapchainImage);

    // transition swapchain image for present
    cmd->barrier(swapchainImage, VkImageLayout::VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);

    cmd->end();
}

void VulkanEngine::submitFrame()
{
    auto& frameData = m_frames[m_frameNumber];

    frameData.fences.push_back(m_graphicsQueue->submit(frameData.mainCommandBuffer->getHandle(), frameData.renderSemaphore, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                                                        frameData.swapchainSemaphore, VK_PIPELINE_STAGE_2_TRANSFER_BIT));

    m_graphicsQueue->present(m_swapchain->getHandle(), m_swapchainIndex, frameData.renderSemaphore);

    ++m_frameNumber %= m_frameCount;
}

void VulkanEngine::run()
{
    SDL_Event e;
    bool bQuit = false;

    // main loop
    while (!bQuit) {
        // Handle events on queue
        while (SDL_PollEvent(&e) != 0) {
            // close the window when user alt-f4s or clicks the X button
            if (e.type == SDL_QUIT) {
                bQuit = true;
                m_stopRendering = true;
            }

            if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                    m_stopRendering = true;
                }
                if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
                    m_stopRendering = false;
                }
            }
        }

        // do not draw if we are minimized
        if (m_stopRendering) {
            // throttle the speed to avoid the endless spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        recordCommands();
        submitFrame();
    }

    delete sm_loadedEngine;
}
