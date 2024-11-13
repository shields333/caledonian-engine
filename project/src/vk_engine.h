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

struct FrameData {

	std::unique_ptr<CommandPool> commandPool;
	CommandBuffer* mainCommandBuffer;

	VkSemaphore swapchainSemaphore, renderSemaphore;
	std::vector<Fence> fences;
};

class VulkanEngine 
{
public:

	DescriptorAllocator m_globalDescriptorAllocator;

	VkDescriptorSet m_drawImageDescriptors;
	VkDescriptorSetLayout m_drawImageDescriptorLayout;

	std::unique_ptr<ComputePipeline> m_pipeline;

	uint32_t m_frameCount = 3;

	uint32_t m_swapchainIndex = 0;

	int m_frameNumber{ 0 };

	std::vector<FrameData> m_frames;

	std::unique_ptr<Image> m_drawImage;

	std::unique_ptr<FencePool> m_fencePool;

	FrameData& getCurrentFrame() { return m_frames[m_frameNumber % m_frameCount]; };

	VmaAllocator m_allocator;

	bool m_stopRendering{ false };
	VkExtent2D m_windowExtent{ 1600 , 900 };

	VkSurfaceKHR m_surface = VK_NULL_HANDLE;// Vulkan window surface

	VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;// Vulkan debug output handle

	VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;// GPU chosen as the default device

	std::unique_ptr<Swapchain> m_swapchain;

	bool m_useValidationLayers = false;

	struct SDL_Window* m_window{ nullptr };

	static VulkanEngine& get();
	static const Device* getDevice();

	VulkanEngine(VulkanEngine& other) = delete;
	VulkanEngine& operator=(const VulkanEngine& other) = delete;

	VulkanEngine(VulkanEngine&& other) noexcept { *this = std::move(other); }
	VulkanEngine& operator=(VulkanEngine&& other) noexcept = default;

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

	std::unique_ptr<Queue>	m_graphicsQueue;

	VulkanEngine();
	~VulkanEngine();

	void initVulkan();
	void initSwapchain();
	void initCommandPools();
	void initSyncStructures();
	void init_descriptors();
	void init_pipelines();
	void init_background_pipelines();

	void destroySwapchain();
};
