// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include "vk_types.h"
#include "vk_descriptors.h"
#include "SceneGraph/SceneGraph.h"

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
#include "Camera.h"

// Render Modules
#include "Modules/Tonemapper.h"
#include "Modules/DownSampler.h"

struct FrameData {

	std::unique_ptr<CommandPool> commandPool;
	CommandBuffer* mainCommandBuffer;

	VkSemaphore swapchainSemaphore;
	std::vector<Fence> fences;

	DescriptorAllocatorGrowable m_frameDescriptors;

	std::unique_ptr<Buffer> m_gpuSceneDataBuffer;
	std::unique_ptr<Buffer> m_objectDataBuffer;
	std::unique_ptr<Buffer> m_drawsBuffer;
};

struct GPUSceneData {
	glm::mat4 view;
	glm::mat4 proj;
	glm::mat4 viewproj;
	glm::vec4 ambientColor;
	glm::vec4 sunlightDirection; // w for sun power
	glm::vec4 sunlightColor;
	glm::vec4 cameraPos;
	glm::vec2  hizSize;
};

struct CullPushConstants {
	std::array<Camera::FrustumPlane, 6> frustums;
	uint32_t		objCount;
	uint32_t		cull;
	VkDeviceAddress inInstanceBuf;
	VkDeviceAddress inTransformBuf;
	VkDeviceAddress inSurfaceBuf;
	VkDeviceAddress outObjBuf;
	VkDeviceAddress outDrawCountBuf;
	VkDeviceAddress outDrawBuf;
};

struct ResetCounterConstants {
	VkDeviceAddress outDrawCountBuf;
	uint32_t numOfCounters;
};

struct LumHistConstants {
	glm::vec4 params;
	VkDeviceAddress histogramBuf;
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
	EngineStats m_stats = {};

	Camera m_mainCamera;

	DescriptorAllocatorGrowable m_globalDescriptorAllocator;

	std::unique_ptr<ComputePipeline> m_cullPipeline;
	std::unique_ptr<ComputePipeline> m_resetPipeline;

	std::unordered_map<std::string, std::shared_ptr<SceneGraph>> loadedScenes;
	std::shared_ptr<SceneGraph> m_activeScene;

	uint32_t m_frameCount = 3;

	int m_frameNumber{ 0 };

	std::vector<FrameData> m_frames;

	bool m_stopUpdate = false;
	glm::vec4 m_frustumPlanes[6];

	GPUSceneData m_sceneData;

	VkExtent2D m_drawExtent;
	float m_renderScale = 1.f;

	std::unique_ptr<Image> m_drawImage;
	std::unique_ptr<Image> m_depthImage;

	std::unique_ptr<Buffer> m_globalUniform;

	std::shared_ptr<Image> m_whiteImage;
	std::shared_ptr<Image> m_blackImage;
	std::shared_ptr<Image> m_greyImage;
	std::shared_ptr<Image> m_errorCheckerboardImage;
	std::unique_ptr<Image> m_hizImage;

	std::unique_ptr<Sampler> m_defaultSamplerLinear;
	std::unique_ptr<Sampler> m_defaultSamplerNearest;
	std::unique_ptr<Sampler> m_samplerMin;

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

	std::unique_ptr<modules::DownSampler> m_downSampleModule;

	void update_scene();

	static VulkanEngine& get();
	static const Device* getDevice();
	static const VkInstance& getInstance();
	static float getTimeDelta() { return m_tDelta.count(); }

	VulkanEngine(VulkanEngine& other) = delete;
	VulkanEngine& operator=(const VulkanEngine& other) = delete;

	VulkanEngine(VulkanEngine&& other) noexcept = default;
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

	std::chrono::steady_clock::time_point m_lastTimePoint;
	static std::chrono::duration<float> m_tDelta;

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

	std::unique_ptr<modules::Tonemapper> m_tonemapModule;

	VulkanEngine();
	~VulkanEngine();

	void initVulkan();
	void initSwapchain();
	void initCommandPools();
	void initSyncStructures();
	void init_descriptors();
	void initPipelines();

	void initDefaultData();
	void loadMeshes();
	void createTextures();

	void drawGeometry(CommandBuffer &cmd);
};
