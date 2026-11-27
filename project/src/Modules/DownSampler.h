#pragma once

#include "VK/vk_types.h"

#include "VK/ComputePipeline.h"
#include "VK/Buffer.h"

class CommandBuffer;
class Image;

namespace modules 
{
	enum downSampleMode {
		MEAN = 0,
		MIN = 1
	};

	class DownSampler
	{
	public:
		DownSampler();
		~DownSampler();

		DownSampler(const DownSampler& other) = delete;
		DownSampler operator=(const DownSampler& other) = delete;

		DownSampler(DownSampler&& other) noexcept;
		DownSampler* operator=(DownSampler&& other) noexcept;

		VkDescriptorSetLayout getDescriptorLayout() { return m_downSamplePipelines[0]->getDescriptorLayout(0); }

		void execute(CommandBuffer* cmd, Image* srcImg, Image* dstImg, VkDescriptorSet descSet, uint32_t firstMip, uint32_t mipCount, downSampleMode mode = MEAN);

	private:

		std::unique_ptr<Buffer> m_atomicBuffer;
		std::array<std::unique_ptr<ComputePipeline>, 2> m_downSamplePipelines;

		struct DownSamplerPushConstants {
			VkDeviceAddress atomicCounter;
			uint32_t baseMip;
			uint32_t mipCount;
			glm::vec2 hizExtents;
		};
	};

}