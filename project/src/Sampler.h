#pragma once

#include "vk_types.h"

class Sampler 
{
public:
	Sampler(VkSamplerCreateInfo &info);
	~Sampler();

	Sampler(Sampler&& other) noexcept;
	Sampler& operator=(Sampler&& other) noexcept;

	VkSampler getHandle() { return m_handle; }

private:

	VkSampler m_handle;
};