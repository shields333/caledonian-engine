// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <vk_descriptors.h>

#include "Device.h"
#include "Swapchain.h"
#include "CommandPool.h"
#include "FencePool.h"
#include "Queue.h"
#include "Image.h"
#include "ComputePipeline.h"
#include "GraphicsPipeline.h"
#include "GuiManager.h"
#include "Mesh.h"

struct FrameData {

	std::unique_ptr<CommandPool> commandPool;
	CommandBuffer* mainCommandBuffer;

	VkSemaphore swapchainSemaphore, renderSemaphore;
	std::vector<Fence> fences;
};

struct ComputePushConstants {
	glm::vec4 data1;
	glm::vec4 data2;
	glm::vec4 data3;
	glm::vec4 data4;
};

struct ComputeEffect {
	const char* name;

	ComputePipeline* pipeline;

	ComputePushConstants data;
};

class VulkanEngine 
{
public:

	DescriptorAllocator m_globalDescriptorAllocator;

	VkDescriptorSet m_drawImageDescriptors;
	VkDescriptorSetLayout m_drawImageDescriptorLayout;

	std::unique_ptr<ComputePipeline> m_gradientPipeline;
	std::unique_ptr<ComputePipeline> m_skyPipeline;

	std::vector<ComputeEffect> m_backgroundEffects;
	int m_currentBackgroundEffect{ 0 };

	std::unique_ptr<GraphicsPipeline> m_trianglePipeline;
	std::unique_ptr<GraphicsPipeline> m_meshPipeline;

	std::unique_ptr<Mesh> m_mesh;

	uint32_t m_frameCount = 3;

	uint32_t m_swapchainIndex = 0;

	int m_frameNumber{ 0 };

	std::vector<FrameData> m_frames;

	std::unique_ptr<Image> m_drawImage;

	std::unique_ptr<FencePool> m_fencePool;

	FrameData& getCurrentFrame() { return m_frames[m_frameNumber % m_frameCount]; };

	VmaAllocator m_allocator;

	bool m_stopRendering{ false };
	VkExtent2D m_windowExtent{ 1280 , 720 };

	VkSurfaceKHR m_surface = VK_NULL_HANDLE;// Vulkan window surface

	VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;// Vulkan debug output handle

	VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;// GPU chosen as the default device

	std::unique_ptr<Swapchain> m_swapchain;

	std::unique_ptr<Queue>	m_graphicsQueue;

	bool m_useValidationLayers = false;

	struct SDL_Window* m_window{ nullptr };

	std::unique_ptr<GuiManager> m_guiManager;

	static VulkanEngine& get();
	static const Device* getDevice();
	static const VkInstance& getInstance();

	VulkanEngine(VulkanEngine& other) = delete;
	VulkanEngine& operator=(const VulkanEngine& other) = delete;

	VulkanEngine(VulkanEngine&& other) noexcept { *this = std::move(other); }
	VulkanEngine& operator=(VulkanEngine&& other) noexcept = default;

	void immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);

	void init();

	void recordCommands();

	//draw loop
	void submitFrame();

	//run main loop
	void run();

private:

	static VulkanEngine* sm_loadedEngine;

	VkInstance m_instance = VK_NULL_HANDLE;// Vulkan library handle

	std::unique_ptr<Device> m_device; // Vulkan device for commands

	std::unique_ptr<CommandPool> m_cmdPool;
	std::unique_ptr<CommandBuffer> m_cmdBuf;

	VulkanEngine();
	~VulkanEngine();

	void initVulkan();
	void initSwapchain();
	void initCommandPools();
	void initSyncStructures();
	void init_descriptors();
	void init_pipelines();
	void init_background_pipelines();

	void initTrianglePipeline();
	void initMeshPipeline();

	void initDefaultData();

	void drawGeometry(CommandBuffer &cmd);

	void destroySwapchain();
};
