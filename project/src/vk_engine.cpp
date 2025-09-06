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

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    m_window = SDL_CreateWindow(
        "Vulkan Engine",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        m_windowExtent.width,
        m_windowExtent.height,
        window_flags);

    SDL_SetRelativeMouseMode(SDL_bool::SDL_TRUE);
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

    initDefaultData();

    init_pipelines();

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
    VkPhysicalDeviceVulkan13Features features13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    features13.dynamicRendering = true;
    features13.synchronization2 = true;

    //vulkan 1.2 features
    VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing = true;
    features12.descriptorBindingPartiallyBound = true;
    features12.drawIndirectCount = true;
    features12.runtimeDescriptorArray = true;

    VkPhysicalDeviceVulkan11Features features11{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    features11.shaderDrawParameters = true;

    VkPhysicalDeviceFeatures features = {};
    features.multiDrawIndirect = true;
    features.shaderInt64 = true;

    //use vkbootstrap to select a gpu. 
    //We want a gpu that can write to the SDL surface and supports vulkan 1.3 with the correct features
    vkb::PhysicalDeviceSelector selector{ vkbInst };
    vkb::PhysicalDevice physicalDevice = selector
        .set_minimum_version(1, 3)
        .set_required_features_13(features13)
        .set_required_features_12(features12)
        .set_required_features_11(features11)
        .set_required_features(features)
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
    m_swapchain = std::make_unique<Swapchain>();
    m_frameCount = m_swapchain->getswapchainImages().size();

    VkImageUsageFlags drawImageUsages{};
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    m_drawImage = std::make_unique<Image>(VK_FORMAT_B10G11R11_UFLOAT_PACK32, VkExtent3D(m_swapchain->m_swapchainExtent.width, m_swapchain->m_swapchainExtent.height, 1),
        drawImageUsages);

    VkImageUsageFlags depthImageUsages{};
    depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    m_depthImage = std::make_unique<Image>(VK_FORMAT_D32_SFLOAT, VkExtent3D(m_swapchain->m_swapchainExtent.width, m_swapchain->m_swapchainExtent.height, 1), 
        depthImageUsages);

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
        frame.m_gpuSceneDataBuffer = std::make_unique<Buffer>(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO);
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
    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes =
    {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }
    };

    m_globalDescriptorAllocator.init(m_device->getHandle(), 10, sizes);

    //make the descriptor set layout for our compute draw
    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        m_drawImageDescriptorLayout = builder.build(m_device->getHandle(), VK_SHADER_STAGE_COMPUTE_BIT);
    }

    //allocate a descriptor set for our draw image
    m_drawImageDescriptors = m_globalDescriptorAllocator.allocate(m_drawImageDescriptorLayout);

    DescriptorWriter writer;
    writer.write_image(0, m_drawImage->getViewHandle(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

    writer.update_set(m_device->getHandle(), m_drawImageDescriptors);

    for (int i = 0; i < m_frameCount; i++) {
        // create a descriptor pool
        std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = {
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
        };

        m_frames[i].m_frameDescriptors = DescriptorAllocatorGrowable{};
        m_frames[i].m_frameDescriptors.init(m_device->getHandle(), 1000, frame_sizes);

        m_deleteionQueue.push_function([&, i]() {
            m_frames[i].m_frameDescriptors.destroy_pools(m_device->getHandle());
            });
    }

    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        m_gpuSceneDataDescriptorLayout = builder.build(m_device->getHandle(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        m_singleImageDescriptorLayout = builder.build(m_device->getHandle(), VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    m_deleteionQueue.push_function([&]() {
        vkDestroyDescriptorSetLayout(m_device->getHandle(), m_singleImageDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(m_device->getHandle(), m_gpuSceneDataDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(m_device->getHandle(), m_drawImageDescriptorLayout, nullptr);
        m_globalDescriptorAllocator.destroy_pools(m_device->getHandle());
        });
}

void VulkanEngine::init_pipelines()
{
    //initMeshPipeline();
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
    m_skyPipeline = std::make_unique<ComputePipeline>("../project/shaders/sky.comp.spv", &layouts, &pushConstant);

    m_sky.pipeline = m_skyPipeline.get();
    m_sky.name = "sky";
    m_sky.data = {};
    //default sky parameters
    m_sky.data.data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);

    pushConstant[0].size = sizeof(CullPushConstants);

    m_cullPipeline = std::make_unique<ComputePipeline>("../project/shaders/culling.comp.spv", nullptr, &pushConstant);

    m_deleteionQueue.push_function([&]() {
        m_cullPipeline.reset();
        m_skyPipeline.reset();
        });
}

//void VulkanEngine::initMeshPipeline()
//{
//}

void VulkanEngine::initDefaultData()
{
    createTextures();

    //write the buffer
    GLTFMetallic_Roughness::MaterialConstants* sceneUniformData = (GLTFMetallic_Roughness::MaterialConstants*)m_globalUniform.get()->getAlloacation()->GetMappedData();
    sceneUniformData->colorFactors = glm::vec4{ 1,1,1,1 };
    sceneUniformData->metal_rough_factors = glm::vec4{ 1,0.5,0,0 };

    loadMeshes();

    for (auto& frame : m_frames) {
        frame.m_objectDataBuffer = std::make_unique<Buffer>(sizeof(ObjectData) * m_activeScene->m_objectCount, VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR, VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO);
        frame.m_drawsBuffer = std::make_unique<Buffer>(sizeof(VkDrawIndexedIndirectCommand) * m_activeScene->m_objectCount + 8, VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT_KHR, VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO);
    }
    m_mainCamera.velocity = glm::vec3(0.f);
    m_mainCamera.position = glm::vec3(0, 0, 5);

    m_mainCamera.pitch = 0;
    m_mainCamera.yaw = 0;
}

void VulkanEngine::loadMeshes()
{
    std::string structurePath = { "..\\project\\assets\\Bistro_Godot.glb" };
    auto structureFile = loadGltf(structurePath);

    assert(structureFile.has_value());

    m_activeScene = loadedScenes["structure"] = *structureFile;

    m_deleteionQueue.push_function([&]() {
        m_activeScene.reset();
        loadedScenes.clear();
        });
}

void VulkanEngine::createTextures()
{
    //3 default textures, white, grey, black. 1 pixel each
    uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
    m_whiteImage = std::make_unique<Image>(VkFormat::VK_FORMAT_R8G8B8A8_UNORM, VkExtent3D{ 1, 1, 1 },
        VK_IMAGE_USAGE_SAMPLED_BIT, (void*)&white);

    uint32_t grey = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1));
    m_greyImage = std::make_unique<Image>(VkFormat::VK_FORMAT_R8G8B8A8_UNORM, VkExtent3D{ 1, 1, 1 },
        VK_IMAGE_USAGE_SAMPLED_BIT, (void*)&grey);

    uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 0));
    m_blackImage = std::make_unique<Image>(VkFormat::VK_FORMAT_R8G8B8A8_UNORM, VkExtent3D{ 1, 1, 1 },
        VK_IMAGE_USAGE_SAMPLED_BIT, (void*)&black);

    //checkerboard image
    uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
    std::array<uint32_t, 16 * 16 > pixels; //for 16x16 checkerboard texture
    for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 16; y++) {
            pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
        }
    }
    m_errorCheckerboardImage = std::make_shared<Image>(VkFormat::VK_FORMAT_R8G8B8A8_UNORM, VkExtent3D{ 16, 16, 1 },
        VK_IMAGE_USAGE_SAMPLED_BIT, (void*)&magenta);

    VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

    sampl.magFilter = VK_FILTER_NEAREST;
    sampl.minFilter = VK_FILTER_NEAREST;
    m_defaultSamplerNearest = std::make_unique<Sampler>(sampl);

    sampl.magFilter = VK_FILTER_LINEAR;
    sampl.minFilter = VK_FILTER_LINEAR;
    sampl.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    m_defaultSamplerLinear = std::make_unique<Sampler>(sampl);

    m_globalUniform = std::make_unique<Buffer>(sizeof(GLTFMetallic_Roughness::MaterialConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    m_deleteionQueue.push_function([&]() {
        m_globalUniform.reset();

        m_defaultSamplerLinear.reset();
        m_defaultSamplerNearest.reset();

        m_errorCheckerboardImage.reset();
        m_blackImage.reset();
        m_greyImage.reset();
        m_whiteImage.reset();
        });
}

void VulkanEngine::update_scene()
{
    //begin clock
    auto start = std::chrono::system_clock::now();

    m_mainCamera.update();

    m_activeScene->updateContext(glm::mat4{ 1.f });

    m_sceneData.view = m_mainCamera.getViewMatrix();
    m_sceneData.proj = m_mainCamera.getProjectionMatrix();
    m_sceneData.viewproj = m_sceneData.proj * m_sceneData.view;

    //some default lighting parameters
    m_sceneData.ambientColor = glm::vec4(.1f);
    m_sceneData.sunlightColor = glm::vec4(1.f);
    m_sceneData.sunlightDirection = glm::vec4(0, 1, 0.5, 1.f);

    if (!m_stopUpdate)
        m_cullFrustum = m_sceneData.viewproj;

    auto end = std::chrono::system_clock::now();

    //convert to microseconds (integer), and then come back to miliseconds
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    m_stats.scene_update_time = elapsed.count() / 1000.f;

}

void VulkanEngine::drawGeometry(CommandBuffer& cmd)
{
    //reset counters
    m_stats.drawcall_count = 0;

    //begin clock
    auto start = std::chrono::system_clock::now();

    //write the buffer
    GPUSceneData* sceneUniformData = (GPUSceneData*)m_frames[m_frameNumber].m_gpuSceneDataBuffer->getAlloacation()->GetMappedData();
    *sceneUniformData = m_sceneData;

    //create a descriptor set that binds that buffer and update it
    VkDescriptorSet globalDescriptor = m_frames[m_frameNumber].m_frameDescriptors.allocate(m_gpuSceneDataDescriptorLayout);

    DescriptorWriter writer;
    writer.write_buffer(0, m_frames[m_frameNumber].m_gpuSceneDataBuffer->getHandle(), sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.update_set(m_device->getHandle(), globalDescriptor);

    m_activeScene->cull(cmd, m_cullFrustum, *m_frames[m_frameNumber].m_objectDataBuffer, *m_frames[m_frameNumber].m_drawsBuffer, m_cullPipeline.get());
    
    //begin a render pass  connected to our draw image
    cmd.beginRendering(m_depthImage.get(), m_drawImage.get());

    //set dynamic viewport and scissor
    cmd.setViewport(m_drawExtent);
    cmd.setScissor(m_drawExtent);

    m_activeScene->draw(cmd, globalDescriptor, m_sceneData.viewproj, *m_frames[m_frameNumber].m_objectDataBuffer, *m_frames[m_frameNumber].m_drawsBuffer);

    cmd.endRendering();

    auto end = std::chrono::system_clock::now();

    //convert to microseconds (integer), and then come back to miliseconds
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    m_stats.mesh_draw_time = elapsed.count() / 1000.f;
}

void VulkanEngine::recordCommands()
{
    update_scene();

    auto& frameData = m_frames[m_frameNumber];

    if (!frameData.fences.empty()) {
        for (auto& fence : frameData.fences) {
            fence.wait(UINT64_MAX);
            fence.reset();
            m_fencePool->returnFence(fence);
        }
        frameData.fences.clear();
    }
    frameData.m_frameDescriptors.clear_pools(m_device->getHandle());

    m_drawExtent.height = std::min(m_swapchain->m_swapchainExtent.height, m_drawImage->getExtent().height) * m_renderScale;
    m_drawExtent.width = std::min(m_swapchain->m_swapchainExtent.width, m_drawImage->getExtent().width) * m_renderScale;

    // acquire the next swapchain image
    m_swapchainIndex = m_swapchain->acquireImage(UINT64_MAX, frameData.swapchainSemaphore);
    Image& swapchainImage = static_cast<Image&>(m_swapchain->getswapchainImages()[m_swapchainIndex]);

    // reset the pool before recording
    frameData.commandPool->resetPool();

    auto& cmd = frameData.mainCommandBuffer;
    cmd->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    // transition image for transfer
    cmd->barrier(*m_drawImage, VkImageLayout::VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT);

    cmd->bindPipeline(m_sky.pipeline->getHandle(), VK_PIPELINE_BIND_POINT_COMPUTE);
    cmd->bindDescriptorSet(m_drawImageDescriptors, m_skyPipeline->getLayout(), VK_PIPELINE_BIND_POINT_COMPUTE);

    cmd->dispatch(std::ceil(m_swapchain->m_swapchainExtent.width / 16.0), std::ceil(m_swapchain->m_swapchainExtent.height / 16.0), 1);

    cmd->barrier(*m_drawImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    cmd->barrier(*m_depthImage, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    drawGeometry(*cmd);

    // transition images for copying
    cmd->barrier(*m_drawImage, VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    cmd->barrier(swapchainImage, VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    // copy offscreen image to swapchain image
    cmd->blit(*m_drawImage, swapchainImage, VkExtent3D(m_drawExtent.width, m_drawExtent.height, 1), swapchainImage.getExtent());

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
        //begin clock
        auto start = std::chrono::system_clock::now();

        // Handle events on queue
        while (SDL_PollEvent(&e) != 0) {
            // close the window when user alt-f4s or clicks the X button or escape key
            if (e.type == SDL_QUIT || e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
                bQuit = true;
                m_stopRendering = true;
            }

            if (e.type == SDL_KEYUP && e.key.keysym.sym == SDLK_SPACE)
                m_stopUpdate = !m_stopUpdate;

            if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                    m_stopRendering = true;
                }
                if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
                    m_stopRendering = false;
                }
            }

            m_mainCamera.processSDLEvent(e);
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

        //get clock again, compare with start clock
        auto end = std::chrono::system_clock::now();

        //convert to microseconds (integer), and then come back to miliseconds
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        m_stats.frametime = elapsed.count() / 1000.f;

    }

    delete sm_loadedEngine;
}
