#include "DownSampler.h"

#include "VK/vk_engine.h"
#include "VK/CommandPool.h"
#include "VK/Image.h"

namespace modules
{
	DownSampler::DownSampler()
	{
		m_downSamplePipeline = std::make_unique<ComputePipeline>(SHADER_PATH + "meanDownsampler.comp.spv");

		m_atomicBuffer = std::make_unique<Buffer>(sizeof(uint32_t), VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR, VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO);
	}

	DownSampler::~DownSampler()
	{
		m_atomicBuffer.reset();

		m_downSamplePipeline.reset();
	}

	DownSampler::DownSampler(DownSampler&& other) noexcept
	{
		*this = std::move(other);
	}

	DownSampler* DownSampler::operator=(DownSampler&& other) noexcept
	{
		m_atomicBuffer = std::move(other.m_atomicBuffer);

		m_downSamplePipeline = std::move(other.m_downSamplePipeline);

		return this;
	}

	void DownSampler::execute(CommandBuffer* cmd, Image* srcImg, VkDescriptorSet descSet, uint32_t firstMip, uint32_t mipCount)
	{
		// reset the counter
		uint32_t* counter = static_cast<uint32_t*>(m_atomicBuffer->getInfo().pMappedData);
		memset(counter, 0, sizeof(uint32_t));
		m_atomicBuffer->srcAccess = VK_ACCESS_2_HOST_WRITE_BIT;
		m_atomicBuffer->srcStage = VK_PIPELINE_STAGE_2_HOST_BIT;

		DownSamplerPushConstants constants = {
			m_atomicBuffer->getAddress(),
			firstMip,
			mipCount
		};

		cmd->barrier(*srcImg, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, 0, 1);
		cmd->barrier(*srcImg, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT, firstMip, mipCount);
		cmd->barrier(*m_atomicBuffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);

		cmd->bindPipeline(m_downSamplePipeline->getHandle(), VK_PIPELINE_BIND_POINT_COMPUTE);

		cmd->bindDescriptorSet(descSet, m_downSamplePipeline->getLayout(), VK_PIPELINE_BIND_POINT_COMPUTE);
		cmd->pushConstants(&constants, sizeof(DownSamplerPushConstants), m_downSamplePipeline->getLayout(), VK_SHADER_STAGE_COMPUTE_BIT);

		VkExtent3D imageSize = srcImg->getExtent();
		cmd->dispatch(imageSize.width / 2, imageSize.height / 2, 1);

		cmd->barrier(*m_atomicBuffer, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT);

		cmd->barrier(*srcImg, VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, firstMip, mipCount);
	}
}