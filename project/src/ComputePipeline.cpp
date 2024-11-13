#include "ComputePipeline.h"

#include <vk_pipelines.h>

#include "vk_engine.h"

ComputePipeline::ComputePipeline(std::string sourcePath, std::vector<VkDescriptorSetLayout> &descriptorLayouts)
{
	VkPipelineLayoutCreateInfo computeLayout{};
	computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	computeLayout.pNext = nullptr;
	computeLayout.pSetLayouts = descriptorLayouts.data();
	computeLayout.setLayoutCount = descriptorLayouts.size();

	VK_CHECK(vkCreatePipelineLayout(VulkanEngine::getDevice()->getHandle(), &computeLayout, nullptr, &m_layout));

	//layout code
	VkShaderModule computeDrawShader;
	if (!vkutil::load_shader_module(sourcePath.c_str(), VulkanEngine::getDevice()->getHandle(), &computeDrawShader))
	{
		fmt::print("Error when building the compute shader \n");
	}

	VkPipelineShaderStageCreateInfo stageinfo{};
	stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageinfo.pNext = nullptr;
	stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stageinfo.module = computeDrawShader;
	stageinfo.pName = "main";

	VkComputePipelineCreateInfo computePipelineCreateInfo{};
	computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	computePipelineCreateInfo.pNext = nullptr;
	computePipelineCreateInfo.layout = m_layout;
	computePipelineCreateInfo.stage = stageinfo;

	VK_CHECK(vkCreateComputePipelines(VulkanEngine::getDevice()->getHandle(), VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &m_handle));

	vkDestroyShaderModule(VulkanEngine::getDevice()->getHandle(), computeDrawShader, nullptr);
}

ComputePipeline::~ComputePipeline()
{
	vkDestroyPipelineLayout(VulkanEngine::getDevice()->getHandle(), m_layout, nullptr);
	vkDestroyPipeline(VulkanEngine::getDevice()->getHandle(), m_handle, nullptr);
}

ComputePipeline::ComputePipeline(ComputePipeline&& other) noexcept
{
	*this = std::move(other);
}

ComputePipeline& ComputePipeline::operator=(ComputePipeline&& other) noexcept
{
	m_layout = std::exchange(other.m_layout, VK_NULL_HANDLE);
	m_handle = std::exchange(other.m_handle, VK_NULL_HANDLE);
	return *this;
}
