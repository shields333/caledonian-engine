#include "ComputePipeline.h"

#include "vk_pipelines.h"

#include "vk_engine.h"

ComputePipeline::ComputePipeline(std::string sourcePath)
{
	//layout code
	VkPipelineShaderStageCreateInfo shaderStageInfo;
	SpvReflectShaderModule reflectModule;
	if (!vkutil::load_shader_module(sourcePath.c_str(), VulkanEngine::getDevice()->getHandle(), shaderStageInfo, &reflectModule))
	{
		fmt::print("Error when building the compute shader \n");
		assert(0);
	}

	m_layout = vkutil::createPiplineLayout(reflectModule, m_descriptorLayouts);

	VkComputePipelineCreateInfo computePipelineCreateInfo{};
	computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	computePipelineCreateInfo.pNext = nullptr;
	computePipelineCreateInfo.layout = m_layout;
	computePipelineCreateInfo.stage = shaderStageInfo;

	VK_CHECK(vkCreateComputePipelines(VulkanEngine::getDevice()->getHandle(), VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &m_handle));

	vkDestroyShaderModule(VulkanEngine::getDevice()->getHandle(), shaderStageInfo.module, nullptr);
	spvReflectDestroyShaderModule(&reflectModule);
}

ComputePipeline::~ComputePipeline()
{
	vkDestroyPipelineLayout(VulkanEngine::getDevice()->getHandle(), m_layout, nullptr);
	vkDestroyPipeline(VulkanEngine::getDevice()->getHandle(), m_handle, nullptr);

	for (auto descriptorLayout : m_descriptorLayouts)
		vkDestroyDescriptorSetLayout(VulkanEngine::getDevice()->getHandle(), descriptorLayout, nullptr);
}

ComputePipeline::ComputePipeline(ComputePipeline&& other) noexcept
{
	*this = std::move(other);
}

ComputePipeline& ComputePipeline::operator=(ComputePipeline&& other) noexcept
{
	m_layout = std::exchange(other.m_layout, VK_NULL_HANDLE);
	m_handle = std::exchange(other.m_handle, VK_NULL_HANDLE);
	m_descriptorLayouts.swap(other.m_descriptorLayouts);

	return *this;
}
