//> includes
#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_initializers.h>
#include <vk_loader.h>

#include <glm/gtx/transform.hpp>
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

const VkInstance& VulkanEngine::getInstance()
{
    assert(sm_loadedEngine->m_instance);
    return sm_loadedEngine->m_instance;
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

    m_deleteionQueue.flush();

    vkb::destroy_debug_utils_messenger(m_instance, m_debugMessenger);

    m_device.reset();

    vkDestroyInstance(m_instance, nullptr);

    SDL_DestroyWindow(m_window);
    
    // clear engine pointer
    sm_loadedEngine = nullptr;
}

void VulkanEngine::immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function)
{
    m_cmdPool->resetPool();

    m_cmdBuf->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    function(m_cmdBuf->getHandle());
    m_cmdBuf->end();

    Fence fence = std::move(VulkanEngine::get().m_graphicsQueue->submit(m_cmdBuf->getHandle()));

    // block until the submit has completed
    fence.wait(UINT32_MAX);

    VulkanEngine::get().m_fencePool->returnFence(fence);
}

void VulkanEngine::init()
{
    initVulkan();

    initSwapchain();

    initCommandPools();

    initSyncStructures();

    init_descriptors();

    init_pipelines();

    loadMeshes();

    m_guiManager = std::make_unique<GuiManager>();

    m_deleteionQueue.push_function([&]() {
        m_guiManager.reset();
        });
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

    m_deleteionQueue.push_function([&]() {
        vmaDestroyAllocator(m_allocator);
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        });
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

    VkImageUsageFlags depthImageUsages{};
    depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    m_depthImage = std::make_unique<Image>(VK_FORMAT_D32_SFLOAT, m_windowExtent, depthImageUsages, VkImageAspectFlagBits::VK_IMAGE_ASPECT_DEPTH_BIT);

    m_deleteionQueue.push_function([&]() {
        m_depthImage.reset();
        m_drawImage.reset();
        m_swapchain.reset();
        });
}

void VulkanEngine::initCommandPools()
{
    m_frames.resize(m_frameCount);

    for (auto& frame : m_frames) {
        frame.commandPool = std::make_unique<CommandPool>(m_graphicsQueue->m_familyIndex);
        frame.mainCommandBuffer = frame.commandPool->requestCommandBuffer();
    }

    m_cmdPool = std::make_unique<CommandPool>(m_graphicsQueue->m_familyIndex);
    m_cmdBuf = std::make_unique<CommandBuffer>(*m_cmdPool->requestCommandBuffer());

    m_deleteionQueue.push_function([&]() {
        m_cmdBuf.reset();
        m_cmdPool.reset();
        m_frames.clear();
        });
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

    m_fencePool = std::make_unique<FencePool>();

    m_deleteionQueue.push_function([&]() {
        m_fencePool.reset();
        for (auto& frame : m_frames) {
            vkDestroySemaphore(m_device->getHandle(), frame.renderSemaphore, nullptr);
            vkDestroySemaphore(m_device->getHandle(), frame.swapchainSemaphore, nullptr);
        }
        });
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

    m_deleteionQueue.push_function([&]() {
        vkDestroyDescriptorSetLayout(m_device->getHandle(), m_drawImageDescriptorLayout, nullptr);
        m_globalDescriptorAllocator.destroy_pool(m_device->getHandle());
        });
}

void VulkanEngine::init_pipelines()
{
    initMeshPipeline();
    init_background_pipelines();
}

void VulkanEngine::init_background_pipelines()
{
    // create gradient pipeline
    std::vector<VkDescriptorSetLayout> layouts(1, m_drawImageDescriptorLayout);
    std::vector <VkPushConstantRange> pushConstant(1);
    pushConstant[0].offset = 0;
    pushConstant[0].size = sizeof(ComputePushConstants);
    pushConstant[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    m_gradientPipeline = std::make_unique<ComputePipeline>("../project/shaders/gradient_color.comp.spv", layouts, &pushConstant);
    m_skyPipeline = std::make_unique<ComputePipeline>("../project/shaders/sky.comp.spv", layouts, &pushConstant);

    ComputeEffect gradient;
    gradient.pipeline = m_gradientPipeline.get();
    gradient.name = "gradient";
    gradient.data = {};

    //default colors
    gradient.data.data1 = glm::vec4(1, 0, 0, 1);
    gradient.data.data2 = glm::vec4(0, 0, 1, 1);

    ComputeEffect sky;
    sky.pipeline = m_skyPipeline.get();
    sky.name = "sky";
    sky.data = {};
    //default sky parameters
    sky.data.data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);

    //add the 2 background effects into the array
    m_backgroundEffects.push_back(gradient);
    m_backgroundEffects.push_back(sky);

    m_deleteionQueue.push_function([&]() {
        m_skyPipeline.reset();
        m_gradientPipeline.reset();
        });
}

void VulkanEngine::initMeshPipeline()
{
    std::vector <VkPushConstantRange> pushConstant(1);
    pushConstant[0].offset = 0;
    pushConstant[0].size = sizeof(Mesh::GPUDrawPushConstants);
    pushConstant[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    PipelineBuilder builder;

    builder.setShaders("../project/shaders/coloured_triangle_mesh.vert.spv", "../project/shaders/coloured_triangle.frag.spv");
    builder.createPipelineLayout(nullptr, &pushConstant);
    builder.setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    builder.setPolygonMode(VK_POLYGON_MODE_FILL);
    builder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);

    builder.setColourAttachmentFormat(m_drawImage->getFormat());
    builder.setDepthFormat(m_depthImage->getFormat());
    builder.enableDepthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

    m_meshPipeline.reset(builder.buildPipeline());

    m_deleteionQueue.push_function([&]() {
        m_meshPipeline.reset();
        });
}

void VulkanEngine::loadMeshes()
{
    m_testMeshes = loadGltfMeshes(this, "../project/assets/basicmesh.glb").value();

    m_deleteionQueue.push_function([&]() {
        m_testMeshes.clear();
        });
}

void VulkanEngine::drawGeometry(CommandBuffer& cmd)
{
    //begin a render pass  connected to our draw image
    cmd.beginRendering(m_depthImage.get(), m_drawImage.get());

    // bind mesh pipeline
    cmd.bindPipeline(m_meshPipeline->getHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS);

    //set dynamic viewport and scissor
    cmd.setViewport(m_drawImage->getExtent());
    cmd.setScissor(m_drawImage->getExtent());

    glm::mat4 view = glm::translate(glm::vec3{ 0,0,-5 });
    // camera projection
    glm::mat4 projection = glm::perspective(glm::radians(70.f), (float)m_windowExtent.width / (float)m_windowExtent.height, 10000.f, 0.1f);

    // invert the Y direction on projection matrix so that we are more similar
    // to opengl and gltf axis
    projection[1][1] *= -1;

    m_testMeshes[2]->getPushConstants().worldMatrix = projection * view;

    cmd.pushConstants(&m_testMeshes[2]->getPushConstants(), sizeof(Mesh::GPUDrawPushConstants), m_meshPipeline->getLayout(), VK_SHADER_STAGE_VERTEX_BIT);
    cmd.bindIndexBuffer(m_testMeshes[2]->getIndexBuffer());

    auto surfaces = m_testMeshes[2]->getSurfaces();
    cmd.drawIndexed(surfaces[2].count, 1, surfaces[2].startIndex, 0, 0);

    cmd.endRendering();
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

    ComputeEffect& effect = m_backgroundEffects[m_currentBackgroundEffect];
    cmd->bindPipeline(effect.pipeline->getHandle(), VK_PIPELINE_BIND_POINT_COMPUTE);
    cmd->bindDescriptorSet(m_drawImageDescriptors, m_gradientPipeline->getLayout(), VK_PIPELINE_BIND_POINT_COMPUTE);

    cmd->pushConstants(&effect.data, sizeof(ComputePushConstants), effect.pipeline->getLayout(), VK_SHADER_STAGE_COMPUTE_BIT);

    cmd->dispatch(std::ceil(m_windowExtent.width / 16.0), std::ceil(m_windowExtent.height / 16.0), 1);

    cmd->barrier(*m_drawImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    cmd->barrier(*m_depthImage, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    drawGeometry(*cmd);

    // transition images for copying
    cmd->barrier(*m_drawImage, VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    cmd->barrier(swapchainImage, VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    // copy offscreen image to swapchain image
    cmd->blit(*m_drawImage, swapchainImage);

    // begin UI pass
    cmd->barrier(swapchainImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    cmd->beginRendering(nullptr, &swapchainImage);

    // draw the GUI
    m_guiManager->draw(*cmd);

    // end the UI pass
    cmd->endRendering();

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

            //send SDL event to imgui for handling
            m_guiManager->processEvents(e);
        }

        // do not draw if we are minimized
        if (m_stopRendering) {
            // throttle the speed to avoid the endless spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        m_guiManager->newFrame();

        recordCommands();
        submitFrame();
    }

    delete sm_loadedEngine;
}
