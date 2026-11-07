#include "Tonemapper.h"

#include "VK/vk_engine.h"
#include "VK/CommandPool.h"
#include "VK/Image.h"

namespace modules
{
	Tonemapper::Tonemapper(const Image* img)
	{
		m_lumHistPipeline = std::make_unique<ComputePipeline>(SHADER_PATH + "lumHist.comp.spv");
		m_lumAvgPipeline = std::make_unique<ComputePipeline>(SHADER_PATH + "lumAvg.comp.spv");

		PipelineBuilder builder;

		std::vector<std::string> shaderPaths = { SHADER_PATH + "fullscreen.vert.spv", SHADER_PATH + "tonemap.frag.spv" };
		builder.setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
		builder.setPolygonMode(VK_POLYGON_MODE_FILL);
		builder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
		builder.setColourAttachmentFormat(VulkanEngine::get().m_swapchain->m_swapchainImageFormat);

		m_tonemapPipeline.reset(builder.buildPipeline(shaderPaths));

		// create histogram buffer
		m_lumHistogram = std::make_unique<Buffer>(sizeof(uint32_t) * (256 + 1), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO);

		// create a descriptor set for the scene image
		m_sceneTexSet = VulkanEngine::get().m_globalDescriptorAllocator.allocate(m_tonemapPipeline->getDescriptorLayout(0));
		DescriptorWriter writer;
		writer.writeImage(0, img->getViewHandle(), VulkanEngine::get().m_defaultSamplerLinear->getHandle(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		writer.update_set(VulkanEngine::getDevice()->getHandle(), m_sceneTexSet);

		m_lumHistTexSet = VulkanEngine::get().m_globalDescriptorAllocator.allocate(m_lumHistPipeline->getDescriptorLayout(0));
		writer.update_set(VulkanEngine::getDevice()->getHandle(), m_lumHistTexSet);

		m_inputImage = img;
	}

	Tonemapper::~Tonemapper()
	{
		m_lumHistogram.reset();

		m_lumAvgPipeline.reset();
		m_lumHistPipeline.reset();
		m_tonemapPipeline.reset();
	}

	Tonemapper::Tonemapper(Tonemapper&& other) noexcept
	{
		*this = std::move(other);
	}

	Tonemapper* Tonemapper::operator=(Tonemapper&& other) noexcept
	{
		m_lumHistogram = std::move(other.m_lumHistogram);

		m_lumAvgPipeline = std::move(other.m_lumAvgPipeline);
		m_lumHistPipeline = std::move(other.m_lumHistPipeline);
		m_tonemapPipeline = std::move(other.m_tonemapPipeline);

		m_sceneTexSet = other.m_sceneTexSet;
		m_lumHistTexSet = other.m_lumHistTexSet;

		return this;
	}

	void Tonemapper::createHistogram(CommandBuffer* cmd)
	{
		LumHistConstants pushConstants;
		pushConstants.params = glm::vec4(-10.f, 1.f / 12.f, VulkanEngine::get().m_stats.frametime / 1000.f, m_inputImage->getExtent().width * m_inputImage->getExtent().height);
		pushConstants.histogramBuf = m_lumHistogram->getAddress();

		cmd->bindPipeline(m_lumHistPipeline->getHandle(), VK_PIPELINE_BIND_POINT_COMPUTE);
		cmd->bindDescriptorSet(m_lumHistTexSet, m_lumHistPipeline->getLayout(), VK_PIPELINE_BIND_POINT_COMPUTE);
		cmd->pushConstants(&pushConstants, sizeof(LumHistConstants), m_lumHistPipeline->getLayout(), VK_SHADER_STAGE_COMPUTE_BIT);
		cmd->dispatch((VulkanEngine::get().m_drawImage->getExtent().width + 15) / 16, (m_inputImage->getExtent().height + 15) / 16, 1);

		cmd->barrier(*m_lumHistogram, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);

		pushConstants.params.y = 12.f;

		cmd->bindPipeline(m_lumAvgPipeline->getHandle(), VK_PIPELINE_BIND_POINT_COMPUTE);
		cmd->pushConstants(&pushConstants, sizeof(LumHistConstants), m_lumHistPipeline->getLayout(), VK_SHADER_STAGE_COMPUTE_BIT);
		cmd->dispatch(1, 1, 1);

	}
	void Tonemapper::applyTonemapping(CommandBuffer* cmd)
	{
		LumHistConstants pushConstants;
		pushConstants.params = glm::vec4(-10.f, 1.f / 12.f, VulkanEngine::get().m_stats.frametime / 1000.f, m_inputImage->getExtent().width * m_inputImage->getExtent().height);
		pushConstants.histogramBuf = m_lumHistogram->getAddress();

		cmd->bindPipeline(m_tonemapPipeline->getHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS);
		cmd->bindDescriptorSet(m_sceneTexSet, m_tonemapPipeline->getLayout(), VK_PIPELINE_BIND_POINT_GRAPHICS);
		cmd->pushConstants(&pushConstants, sizeof(LumHistConstants), m_tonemapPipeline->getLayout(), VK_SHADER_STAGE_FRAGMENT_BIT);
		cmd->draw(3, 1, 0, 0);
	}
}