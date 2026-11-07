#pragma once

#include "VK/vk_types.h"

#include "VK/ComputePipeline.h"
#include "VK/Buffer.h"

class CommandBuffer;
class Image;

namespace modules 
{
	class DownSampler
	{
	public:
		DownSampler();
		~DownSampler();

		DownSampler(const DownSampler& other) = delete;
		DownSampler operator=(const DownSampler& other) = delete;

		DownSampler(DownSampler&& other) noexcept;
		DownSampler* operator=(DownSampler&& other) noexcept;

		VkDescriptorSetLayout getDescriptorLayout() { return m_downSamplePipeline->getDescriptorLayout(0); }

		void execute(CommandBuffer* cmd, Image* srcImg, VkDescriptorSet descSet, uint32_t firstMip, uint32_t mipCount);

	private:

		std::unique_ptr<Buffer> m_atomicBuffer;
		std::unique_ptr<ComputePipeline> m_downSamplePipeline;

		struct DownSamplerPushConstants {
			VkDeviceAddress atomicCounter;
			uint32_t baseMip;
			uint32_t mipCount;
		};
	};

}