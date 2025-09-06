#pragma once

#include <vk_types.h>

class ComputePipeline
{
public:
	ComputePipeline() = default;
	ComputePipeline(std::string sourcePath, std::vector<VkDescriptorSetLayout> *descriptorLayouts, std::vector<VkPushConstantRange> *pushConstants = nullptr);
	~ComputePipeline();

	ComputePipeline(const ComputePipeline& other) = delete;
	ComputePipeline operator=(const ComputePipeline& other) = delete;

	ComputePipeline(ComputePipeline&& other) noexcept;
	ComputePipeline& operator=(ComputePipeline&& other) noexcept;

	VkPipeline getHandle() { return m_handle; }
	VkPipelineLayout getLayout() { return m_layout; }

private:
	VkPipeline m_handle = VK_NULL_HANDLE;

	VkPipelineLayout m_layout = VK_NULL_HANDLE;
};