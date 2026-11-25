//> includes
#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include "vk_initializers.h"
#include "SceneGraph/GLTFLoader.h"

#include <glm/gtx/transform.hpp>
//bootstrap library
#include "VkBootstrap.h"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include <chrono>
#include <thread>
#include <mutex>

VulkanEngine* VulkanEngine::sm_loadedEngine = nullptr;
std::chrono::duration<float> VulkanEngine::m_tDelta;

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
    //@TODO: parallelise this - needs 1 commandpool per thread
    static std::mutex mtx;
    std::unique_lock<std::mutex> lock(mtx);

    CommandBuffer* cmdBuf = m_cmdPool->requestCommandBuffer();

    cmdBuf->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    function(cmdBuf->getHandle());
    cmdBuf->end();

    Fence fence = std::move(VulkanEngine::get().m_graphicsQueue->submit(cmdBuf->getHandle()));

    // block until the submit has completed
    fence.wait(UINT32_MAX);
    
    m_cmdPool->returnCommandBuffer(&cmdBuf);
    lock.unlock();

    VulkanEngine::get().m_fencePool->returnFence(fence);
}

void VulkanEngine::init()
{
    initVulkan();

    m_downSampleModule = std::make_unique<modules::DownSampler>();

    initSwapchain();

    initCommandPools();

    initSyncStructures();

    init_descriptors();

    initDefaultData();

    initPipelines();

    m_tonemapModule = std::make_unique<modules::Tonemapper>(m_drawImage.get());

    m_guiManager = std::make_unique<GuiManager>();

    m_deleteionQueue.push_function([&]() {
        m_guiManager.reset();
        m_tonemapModule.reset();
        m_downSampleModule.reset();
        });
}

void VulkanEngine::initVulkan()
{
    volkInitialize();

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

    volkLoadInstance(m_instance);

    SDL_Vulkan_CreateSurface(m_window, m_instance, &m_surface);

    //vulkan 1.3 features
    VkPhysicalDeviceVulkan13Features features13{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    features13.dynamicRendering = true;
    features13.synchronization2 = true;
    features13.shaderDemoteToHelperInvocation = true;

    //vulkan 1.2 features
    VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing = true;
    features12.descriptorBindingPartiallyBound = true;
    features12.drawIndirectCount = true;
    features12.runtimeDescriptorArray = true;
    features12.samplerFilterMinmax = true;

    VkPhysicalDeviceVulkan11Features features11{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    features11.shaderDrawParameters = true;

    VkPhysicalDeviceFeatures features = {};
    features.multiDrawIndirect = true;
    features.shaderInt64 = true;

    //use vkbootstrap to select a gpu. 
    //We want a gpu that can write to the SDL surface and supports vulkan 1.3 with the correct features
    vkb::PhysicalDeviceSelector selector{ vkbInst };
    vkb::PhysicalDevice physicalDevice = selector
        .set_minimum_version(1, 4)
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

    volkLoadDevice(m_device->getHandle());

    // use vkbootstrap to get a Graphics queue
    m_graphicsQueue = std::make_unique<Queue>(vkbDevice.get_queue(vkb::QueueType::graphics).value(),
        vkbDevice.get_queue_index(vkb::QueueType::graphics).value());

    // initialize the memory allocator
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = m_physicalDevice;
    allocatorInfo.device = m_device->getHandle();
    allocatorInfo.instance = m_instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    VmaVulkanFunctions vmaVulkanFuncs{ .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
    .vkGetDeviceProcAddr = vkGetDeviceProcAddr };
    allocatorInfo.pVulkanFunctions = &vmaVulkanFuncs;

    vmaCreateAllocator(&allocatorInfo, &m_allocator);
    
    m_deleteionQueue.push_function([&]() {
        vmaDestroyAllocator(m_allocator);
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        });
}

void VulkanEngine::initSwapchain()
{
    m_swapchain = std::make_unique<Swapchain>();
    m_frameCount = m_swapchain->getImageCount();

    VkImageUsageFlags drawImageUsages{};
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    drawImageUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;

    m_drawImage = std::make_unique<Image>(VK_FORMAT_B10G11R11_UFLOAT_PACK32, VkExtent3D(m_swapchain->m_swapchainExtent.width, m_swapchain->m_swapchainExtent.height, 1),
        drawImageUsages);

    VkImageUsageFlags depthImageUsages{};
    depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

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

    m_cmdPool = std::make_unique<CommandPool>(m_graphicsQueue->m_familyIndex, VkCommandPoolCreateFlagBits::VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    m_deleteionQueue.push_function([&]() {
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
    }

    m_fencePool = std::make_unique<FencePool>();

    m_deleteionQueue.push_function([&]() {
        m_fencePool.reset();
        for (auto& frame : m_frames) {
            vkDestroySemaphore(m_device->getHandle(), frame.swapchainSemaphore, nullptr);
        }
        });
}

void VulkanEngine::init_descriptors()
{
    //create a descriptor pool that will hold 10 sets with 1 image each
    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes =
    {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 12 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }
    };

    m_globalDescriptorAllocator.init(m_device->getHandle(), 10, sizes);

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

    m_deleteionQueue.push_function([&]() {
        m_globalDescriptorAllocator.destroy_pools(m_device->getHandle());
        });
}

void VulkanEngine::initPipelines()
{

    m_cullPipeline = std::make_unique<ComputePipeline>(SHADER_PATH + "culling.comp.spv");
    m_resetPipeline = std::make_unique<ComputePipeline>(SHADER_PATH + "resetDraw.comp.spv");

    m_deleteionQueue.push_function([&]() {
        m_cullPipeline.reset();
        m_resetPipeline.reset();
        });
}

void VulkanEngine::initDefaultData()
{
    createTextures();

    //write the buffer
    GLTFMetallic_Roughness::MaterialConstants* sceneUniformData = (GLTFMetallic_Roughness::MaterialConstants*)m_globalUniform.get()->getAlloacation()->GetMappedData();
    sceneUniformData->colourFactors = glm::vec4{ 1,1,1,1 };
    sceneUniformData->metalRoughFactors = glm::vec4{ 1,0.5,0,0 };

    loadMeshes();

    for (auto& frame : m_frames) {
        frame.m_objectDataBuffer = std::make_unique<Buffer>(sizeof(ObjectData) * m_activeScene->m_objectCount, VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR, VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO);
        frame.m_drawsBuffer = std::make_unique<Buffer>(sizeof(VkDrawIndexedIndirectCommand) * m_activeScene->m_objectCount + 12, VK_BUFFER_USAGE_2_INDIRECT_BUFFER_BIT_KHR, VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO);
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
    VkSamplerReductionModeCreateInfo reductMode = { .sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO };
    reductMode.reductionMode = VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE;

    VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sampl.pNext = &reductMode;

    sampl.magFilter = VK_FILTER_NEAREST;
    sampl.minFilter = VK_FILTER_NEAREST;
    m_defaultSamplerNearest = std::make_unique<Sampler>(sampl);

    sampl.magFilter = VK_FILTER_NEAREST;
    sampl.minFilter = VK_FILTER_NEAREST;
    sampl.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    m_defaultSamplerLinear = std::make_unique<Sampler>(sampl);

    reductMode.reductionMode = VK_SAMPLER_REDUCTION_MODE_MAX;
    sampl.magFilter = VK_FILTER_LINEAR;
    sampl.minFilter = VK_FILTER_LINEAR;
    sampl.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    m_samplerMax = std::make_unique<Sampler>(sampl);

    //3 default textures, white, grey, black. 1 pixel each
    uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
    m_whiteImage = std::make_unique<Image>(VkFormat::VK_FORMAT_R8G8B8A8_UNORM, VkExtent3D{ 1, 1, 1 },
        VK_IMAGE_USAGE_SAMPLED_BIT, (void*)&white);
    m_whiteImage->setSampler(m_defaultSamplerNearest->getHandle());

    uint32_t grey = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1));
    m_greyImage = std::make_unique<Image>(VkFormat::VK_FORMAT_R8G8B8A8_UNORM, VkExtent3D{ 1, 1, 1 },
        VK_IMAGE_USAGE_SAMPLED_BIT, (void*)&grey);
    m_greyImage->setSampler(m_defaultSamplerNearest->getHandle());

    uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 0));
    m_blackImage = std::make_unique<Image>(VkFormat::VK_FORMAT_R8G8B8A8_UNORM, VkExtent3D{ 1, 1, 1 },
        VK_IMAGE_USAGE_SAMPLED_BIT, (void*)&black);
    m_blackImage->setSampler(m_defaultSamplerNearest->getHandle());

    m_hizImage = std::make_unique<Image>(VkFormat::VK_FORMAT_R32_SFLOAT, VkExtent3D{ m_depthImage->getExtent().width / 2, m_depthImage->getExtent().height / 2, 1},
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_2_STORAGE_BIT_KHR, nullptr, true);
    m_hizImage->setSampler(m_samplerMax->getHandle());

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
    m_errorCheckerboardImage->setSampler(m_defaultSamplerNearest->getHandle());

    m_globalUniform = std::make_unique<Buffer>(sizeof(GLTFMetallic_Roughness::MaterialConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO);

    m_deleteionQueue.push_function([&]() {
        m_globalUniform.reset();

        m_defaultSamplerLinear.reset();
        m_defaultSamplerNearest.reset();
        m_samplerMax.reset();

        m_errorCheckerboardImage.reset();
        m_blackImage.reset();
        m_greyImage.reset();
        m_whiteImage.reset();
        m_hizImage.reset();
        });
}

void VulkanEngine::update_scene()
{
    m_tDelta = std::chrono::duration_cast<std::chrono::duration<float>>(std::chrono::steady_clock::now() - m_lastTimePoint);
    m_lastTimePoint = std::chrono::steady_clock::now();

    //begin clock
    auto start = std::chrono::steady_clock::now();

    m_mainCamera.update();

    m_activeScene->updateContext(glm::mat4{ 1.f });

    m_sceneData.view = m_mainCamera.getViewMatrix();
    m_sceneData.proj = m_mainCamera.getProjectionMatrix();
    m_sceneData.viewproj = m_mainCamera.m_viewProj;
    m_sceneData.cameraPos = glm::vec4(m_mainCamera.position, 1.0);

    //some default lighting parametersaa
    m_sceneData.ambientColor = glm::vec4(.1f);
    m_sceneData.sunlightColor = glm::vec4(glm::vec3(255.f, 255.f, 251.f) / 255.f, 3.f);
    m_sceneData.sunlightDirection = glm::vec4(0, -0.5, 0.5, 1.f);

    auto end = std::chrono::steady_clock::now();

    //convert to microseconds (integer), and then come back to miliseconds
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    m_stats.scene_update_time = elapsed.count() / 1000.f;

}

void VulkanEngine::drawGeometry(CommandBuffer& cmd)
{
    if (!m_stopUpdate)
        //reset counters
        m_stats.drawcall_count = 0;

    //begin clock
    auto start = std::chrono::steady_clock::now();

    //write the buffer
    GPUSceneData* sceneUniformData = (GPUSceneData*)m_frames[m_frameNumber].m_gpuSceneDataBuffer->getInfo().pMappedData;
    *sceneUniformData = m_sceneData;

    //create a descriptor set that binds that buffer and update it
    VkDescriptorSet globalDescriptor = m_frames[m_frameNumber].m_frameDescriptors.allocate(m_activeScene->m_gpuSceneDataDescriptorLayout);

    DescriptorWriter writer;
    writer.write_buffer(0, m_frames[m_frameNumber].m_gpuSceneDataBuffer->getHandle(), sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.update_set(m_device->getHandle(), globalDescriptor);

    {
        if (!m_stopUpdate) {
            cmd.barrier(*m_hizImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, 0, m_hizImage->getMipCount());

            VkDescriptorSet cullDescriptor = m_frames[m_frameNumber].m_frameDescriptors.allocate(m_cullPipeline->getDescriptorLayout(0));
            writer.clear();
            writer.writeImage(0, m_hizImage->getViewHandle(), m_defaultSamplerNearest->getHandle(), VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VkDescriptorType::VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            writer.update_set(m_device->getHandle(), cullDescriptor);
            m_activeScene->cull(cmd, *m_frames[m_frameNumber].m_objectDataBuffer, *m_frames[m_frameNumber].m_drawsBuffer, m_cullPipeline.get(), cullDescriptor, 0);
        }
        //begin a render pass  connected to our draw image
        VkClearColorValue clearColour = { 0.f, 0.f, 0.f, 0.f };
        cmd.beginRendering(m_depthImage.get(), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, 
            m_drawImage.get(), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, &clearColour);

        //set dynamic viewport and scissor
        cmd.setViewport(m_drawExtent);
        cmd.setScissor(m_drawExtent);

        m_activeScene->draw(cmd, globalDescriptor, m_sceneData.viewproj, *m_frames[m_frameNumber].m_objectDataBuffer, *m_frames[m_frameNumber].m_drawsBuffer);

        cmd.endRendering();
    }

    std::vector<VkImageView> mipViews = m_hizImage->getMipMapViews();
    VkDescriptorSet hizDescriptor = m_frames[m_frameNumber].m_frameDescriptors.allocate(m_downSampleModule->getDescriptorLayout());
    writer.clear();
    writer.writeImage(0, m_depthImage->getViewHandle(), VulkanEngine::get().m_samplerMax->getHandle(), VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VkDescriptorType::VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.writeImageArray(1, mipViews, VulkanEngine::get().m_samplerMax->getHandle(), VkImageLayout::VK_IMAGE_LAYOUT_GENERAL, VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    writer.writeImage(2, mipViews[5], VulkanEngine::get().m_defaultSamplerLinear->getHandle(), VkImageLayout::VK_IMAGE_LAYOUT_GENERAL, VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    writer.update_set(VulkanEngine::getDevice()->getHandle(), hizDescriptor);
    m_downSampleModule->execute(&cmd, m_depthImage.get(), m_hizImage.get(), hizDescriptor, 0, m_hizImage->getMipCount(), modules::MAX);

    cmd.barrier(*m_depthImage, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    
    {
        if (!m_stopUpdate) {
            cmd.barrier(*m_hizImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, 0, m_hizImage->getMipCount());

            VkDescriptorSet cullDescriptor = m_frames[m_frameNumber].m_frameDescriptors.allocate(m_cullPipeline->getDescriptorLayout(0));
            writer.clear();
            writer.writeImage(0, m_hizImage->getViewHandle(), m_defaultSamplerNearest->getHandle(), VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VkDescriptorType::VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            writer.update_set(m_device->getHandle(), cullDescriptor);
            m_activeScene->cull(cmd, *m_frames[m_frameNumber].m_objectDataBuffer, *m_frames[m_frameNumber].m_drawsBuffer, m_cullPipeline.get(), cullDescriptor, 1);
        }
        //begin a render pass  connected to our draw image
        VkClearColorValue clearColour = { 0.f, 0.f, 0.f, 0.f };
        cmd.beginRendering(m_depthImage.get(), VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_DONT_CARE, 
            m_drawImage.get(), VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, nullptr);

        //set dynamic viewport and scissor
        cmd.setViewport(m_drawExtent);
        cmd.setScissor(m_drawExtent);

        m_activeScene->draw(cmd, globalDescriptor, m_sceneData.viewproj, *m_frames[m_frameNumber].m_objectDataBuffer, *m_frames[m_frameNumber].m_drawsBuffer);

        cmd.endRendering();
    }

    auto end = std::chrono::steady_clock::now();

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
    m_swapchain->acquireImage(UINT64_MAX, frameData.swapchainSemaphore);
    Image& swapchainImage = static_cast<Image&>(m_swapchain->getswapchainImage());

    // reset the pool before recording
    frameData.commandPool->resetPool();

    auto& cmd = frameData.mainCommandBuffer;
    cmd->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    cmd->barrier(*m_drawImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    cmd->barrier(*m_depthImage, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    drawGeometry(*cmd);

    cmd->barrier(*m_drawImage, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);

    m_tonemapModule->createHistogram(cmd);

    // begin UI pass
    cmd->barrier(swapchainImage, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    cmd->beginRendering(nullptr, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        &swapchainImage, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE);

    m_tonemapModule->applyTonemapping(cmd);

    // draw the GUI
    m_guiManager->draw(*cmd);

    // end the UI pass
    cmd->endRendering();

    // transition swapchain image for present
    cmd->barrier(swapchainImage, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);

    cmd->end();
}

void VulkanEngine::submitFrame()
{
    auto& frameData = m_frames[m_frameNumber];

    frameData.fences.push_back(m_graphicsQueue->submit(frameData.mainCommandBuffer->getHandle(), m_swapchain->getSemaphore(), VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                                                        frameData.swapchainSemaphore, VK_PIPELINE_STAGE_2_TRANSFER_BIT));

    m_graphicsQueue->present(m_swapchain->getHandle(), m_swapchain->getIndex(), m_swapchain->getSemaphore());

    ++m_frameNumber %= m_frameCount;
}

void VulkanEngine::run()
{
    SDL_Event e;
    bool bQuit = false;

    // main loop
    while (!bQuit) {
        //begin clock
        auto start = std::chrono::steady_clock::now();

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
        auto end = std::chrono::steady_clock::now();

        //convert to microseconds (integer), and then come back to miliseconds
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        m_stats.frametime = elapsed.count() / 1000.f;

    }

    delete sm_loadedEngine;
}
