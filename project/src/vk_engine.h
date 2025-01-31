// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <vk_descriptors.h>
#include <vk_loader.h>

#include "Device.h"
#include "Swapchain.h"
#include "CommandPool.h"
#include "FencePool.h"
#include "Queue.h"
#include "Image.h"
#include "Sampler.h"
#include "ComputePipeline.h"
#include "GraphicsPipeline.h"
#include "GuiManager.h"
#include "Mesh.h"
#include "camera.h"

struct FrameData {

	std::unique_ptr<CommandPool> commandPool;
	CommandBuffer* mainCommandBuffer;

	VkSemaphore swapchainSemaphore, renderSemaphore;
	std::vector<Fence> fences;

	DescriptorAllocatorGrowable m_frameDescriptors;

	std::unique_ptr<Buffer> m_gpuSceneDataBuffer;
};

struct GPUSceneData {
	glm::mat4 view;
	glm::mat4 proj;
	glm::mat4 viewproj;
	glm::vec4 ambientColor;
	glm::vec4 sunlightDirection; // w for sun power
	glm::vec4 sunlightColor;
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

struct GLTFMetallic_Roughness {
	std::unique_ptr<GraphicsPipeline> opaquePipeline;
	std::unique_ptr<GraphicsPipeline> transparentPipeline;

	VkDescriptorSetLayout materialLayout;

	struct MaterialConstants {
		glm::vec4 colorFactors;
		glm::vec4 metal_rough_factors;
		//padding, we need it anyway for uniform buffers
		glm::vec4 extra[14];
	};

	struct MaterialResources {
		Image* colorImage;
		Sampler* colorSampler;
		Image* metalRoughImage;
		Sampler* metalRoughSampler;
		Buffer* dataBuffer;
		uint32_t dataBufferOffset;
	};

	DescriptorWriter writer;

	void build_pipelines();
	void clear_resources();

	MaterialInstance write_material(MaterialPass pass, const MaterialResources& resources, DescriptorAllocatorGrowable& descriptorAllocator);
};

struct EngineStats {
	float frametime;
	int drawcall_count;
	float scene_update_time;
	float mesh_draw_time;
};

class VulkanEngine 
{
public:
	EngineStats m_stats;

	Camera m_mainCamera;

	DescriptorAllocatorGrowable m_globalDescriptorAllocator;

	VkDescriptorSet m_drawImageDescriptors;
	VkDescriptorSetLayout m_drawImageDescriptorLayout;
	VkDescriptorSetLayout m_singleImageDescriptorLayout;

	std::unique_ptr<ComputePipeline> m_gradientPipeline;
	std::unique_ptr<ComputePipeline> m_skyPipeline;

	std::vector<ComputeEffect> m_backgroundEffects;
	int m_currentBackgroundEffect{ 0 };

	std::unique_ptr<GraphicsPipeline> m_meshPipeline;

	std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> loadedScenes;
	std::shared_ptr<LoadedGLTF> m_activeScene;

	uint32_t m_frameCount = 3;

	uint32_t m_swapchainIndex = 0;

	int m_frameNumber{ 0 };

	std::vector<FrameData> m_frames;

	GPUSceneData m_sceneData;
	VkDescriptorSetLayout m_gpuSceneDataDescriptorLayout;

	MaterialInstance defaultData;
	GLTFMetallic_Roughness metalRoughMaterial;

	VkExtent2D m_drawExtent;
	float m_renderScale = 1.f;

	std::unique_ptr<Image> m_drawImage;
	std::unique_ptr<Image> m_depthImage;

	std::unique_ptr<Buffer> m_globalUniform;

	std::unique_ptr<Image> m_whiteImage;
	std::unique_ptr<Image> m_blackImage;
	std::unique_ptr<Image> m_greyImage;
	std::shared_ptr<Image> m_errorCheckerboardImage;

	std::unique_ptr<Sampler> m_defaultSamplerLinear;
	std::unique_ptr<Sampler> m_defaultSamplerNearest;

	std::unique_ptr<FencePool> m_fencePool;

	FrameData& getCurrentFrame() { return m_frames[m_frameNumber % m_frameCount]; };

	VmaAllocator m_allocator;

	bool m_stopRendering{ false };
	VkExtent2D m_windowExtent{ 1280, 720 };

	VkSurfaceKHR m_surface = VK_NULL_HANDLE;// Vulkan window surface

	VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;// Vulkan debug output handle

	VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;// GPU chosen as the default device

	std::unique_ptr<Swapchain> m_swapchain;

	std::unique_ptr<Queue>	m_graphicsQueue;

	bool m_useValidationLayers = false;

	bool resizeRequested;

	struct SDL_Window* m_window{ nullptr };

	std::unique_ptr<GuiManager> m_guiManager;

	void update_scene();

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

	struct DeletionQueue
	{
		std::deque<std::function<void()>> deletors;

		void push_function(std::function<void()>&& function) {
			deletors.push_back(function);
		}

		void flush() {
			// reverse iterate the deletion queue to execute all the functions
			for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
				(*it)(); //call functors
			}

			deletors.clear();
		}
	}m_deleteionQueue;

	VulkanEngine();
	~VulkanEngine();

	void initVulkan();
	void initSwapchain();
	void initCommandPools();
	void initSyncStructures();
	void init_descriptors();
	void init_pipelines();
	void init_background_pipelines();

	void initMeshPipeline();

	void initDefaultData();
	void loadMeshes();
	void createTextures();

	void drawGeometry(CommandBuffer &cmd);
};
