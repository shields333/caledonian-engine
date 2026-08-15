#include "Sampler.h"

#include "vk_engine.h"

Sampler::Sampler(VkSamplerCreateInfo &info)
{
	vkCreateSampler(VulkanEngine::getDevice()->getHandle(), &info, nullptr, &m_handle);
}

Sampler::~Sampler()
{
	vkDestroySampler(VulkanEngine::getDevice()->getHandle(), m_handle, nullptr);
}

Sampler::Sampler(Sampler&& other) noexcept
{
	*this = std::move(other);
}

Sampler& Sampler::operator=(Sampler&& other) noexcept
{
	m_handle = std::exchange(other.m_handle, VK_NULL_HANDLE);
	return *this;
}
