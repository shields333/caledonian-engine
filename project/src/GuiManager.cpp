#include "GuiManager.h"

#include "vk_initializers.h"

#include "vk_engine.h"
#include "FencePool.h"
#include "CommandPool.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

#include <SDL.h>

GuiManager::GuiManager()
{
	// 1: create descriptor pool for IMGUI
	//  the size of the pool is very oversized, but it's copied from imgui demo
	//  itself.
	VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000;
	pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
	pool_info.pPoolSizes = pool_sizes;

	VK_CHECK(vkCreateDescriptorPool(VulkanEngine::getDevice()->getHandle(), &pool_info, nullptr, &m_imguiPool));

	// 2: initialize imgui library

	// this initializes the core structures of imgui
	ImGui::CreateContext();

	// this initializes imgui for SDL
	ImGui_ImplSDL2_InitForVulkan(VulkanEngine::get().m_window);

	// this initializes imgui for Vulkan
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = VulkanEngine::getInstance();
	init_info.PhysicalDevice = VulkanEngine::get().m_physicalDevice;
	init_info.Device = VulkanEngine::getDevice()->getHandle();
	init_info.Queue = VulkanEngine::get().m_graphicsQueue->getHandle();
	init_info.DescriptorPool = m_imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.UseDynamicRendering = true;

	//dynamic rendering parameters for imgui to use
	init_info.PipelineRenderingCreateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &VulkanEngine::get().m_swapchain->m_swapchainImageFormat;


	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info);

	ImGui_ImplVulkan_CreateFontsTexture();
}

GuiManager::~GuiManager()
{
	ImGui_ImplVulkan_Shutdown();

	vkDestroyDescriptorPool(VulkanEngine::getDevice()->getHandle(), m_imguiPool, nullptr);
}

GuiManager::GuiManager(GuiManager&& other) noexcept
{
	*this = std::move(other);
}

GuiManager* GuiManager::operator=(GuiManager&& other) noexcept
{
	this->m_imguiPool = std::exchange(other.m_imguiPool, VK_NULL_HANDLE);

	return this;
}

void GuiManager::processEvents(SDL_Event& e)
{
	ImGui_ImplSDL2_ProcessEvent(&e);
}

void GuiManager::newFrame()
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();

	//some imgui UI to test
	//ImGui::ShowDemoWindow();

	if (ImGui::Begin("background")) {

		ImGui::SliderFloat("Render Scale", &VulkanEngine::get().m_renderScale, 0.3f, 1.f);

		ComputeEffect& selected = VulkanEngine::get().m_backgroundEffects[VulkanEngine::get().m_currentBackgroundEffect];

		ImGui::Text("Selected effect: ", selected.name);

		ImGui::SliderInt("Effect Index", &VulkanEngine::get().m_currentBackgroundEffect, 0, VulkanEngine::get().m_backgroundEffects.size() - 1);

		ImGui::InputFloat4("data1", (float*)&selected.data.data1);
		ImGui::InputFloat4("data2", (float*)&selected.data.data2);
		ImGui::InputFloat4("data3", (float*)&selected.data.data3);
		ImGui::InputFloat4("data4", (float*)&selected.data.data4);
	}
	ImGui::End();

	//make imgui calculate internal draw structures
	ImGui::Render();
}

void GuiManager::draw(CommandBuffer& cmd)
{
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd.getHandle());
}
