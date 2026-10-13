#pragma once

#include "VK/vk_types.h"

#include "VK/ComputePipeline.h"
#include "VK/GraphicsPipeline.h"
#include "VK/Buffer.h"

class CommandBuffer;
class Image;

namespace modules 
{
	class Tonemapper
	{
	public:
		Tonemapper(const Image* img);
		~Tonemapper();

		Tonemapper(const Tonemapper& other) = delete;
		Tonemapper operator=(const Tonemapper& other) = delete;

		Tonemapper(Tonemapper&& other) noexcept;
		Tonemapper* operator=(Tonemapper&& other) noexcept;

		void createHistogram(CommandBuffer* cmd);
		void applyTonemapping(CommandBuffer* cmd);

	private:

		std::unique_ptr<ComputePipeline> m_lumHistPipeline;
		std::unique_ptr<ComputePipeline> m_lumAvgPipeline;
		std::unique_ptr<GraphicsPipeline> m_tonemapPipeline;

		std::unique_ptr<Buffer> m_lumHistogram;

		VkDescriptorSet	m_sceneTexSet;
		VkDescriptorSet	m_lumHistTexSet;

		const Image* m_inputImage;
	};

}